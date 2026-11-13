<?php
//Use the usaspending.gov API to check on high profile contracts and report on changes like payments.
//
//hv_contracts columns:
//  hv_watch_children   1 = this is an IDV; discover new task/delivery orders under it and watch them too
//  hv_seeded           0 = first sync not done yet; load its history silently instead of alerting on every old mod
//  hv_children_seeded  0 = first child discovery not done yet; add existing task orders silently
//  hv_active           0 = skip this contract
//
//Optional: php contract_monitor.php --quiet   (sync everything, send no alerts)

include 'common.inc';

$quiet		= in_array('--quiet', $argv);
$alerts_sent	= 0;
$max_alerts	= 25;	//Safety valve so a bad run can't spam Discord

//Update last_run
mysqli_query($conn, "UPDATE control_data SET control_data = NOW() WHERE control_name = 'contract_monitor_lastrun'");

//1. Discover new task/delivery orders under watched IDVs
$result	= mysqli_query($conn, "SELECT * FROM hv_contracts WHERE hv_active = 1 AND hv_watch_children = 1");
while ($row = mysqli_fetch_assoc($result))
{
	discover_children($conn, $row);
}

//2. Check every active contract for new modifications
$result	= mysqli_query($conn, "SELECT * FROM hv_contracts WHERE hv_active = 1 ORDER BY hv_seeded ASC, hv_contract_id");
while ($row = mysqli_fetch_assoc($result))
{
	echo "Checking on {$row['hv_contract_id']} ({$row['hv_contract_description']})...\n";
	update_contract_mods($conn, $row);
	usleep(250000);
}

echo "Done. $alerts_sent alert(s) sent.\n";


function usaspending_post($endpoint, $postData)
{
	//POST to the USAspending API.  Returns decoded array, or NULL on any failure (never dies, so one bad call can't stop the run)
	$ch	= curl_init('https://api.usaspending.gov/api/v2/'.$endpoint);
	curl_setopt_array($ch, array(
		CURLOPT_POST		=> TRUE,
		CURLOPT_RETURNTRANSFER	=> TRUE,
		CURLOPT_TIMEOUT		=> 90,
		CURLOPT_HTTPHEADER	=> array('Content-Type: application/json'),
		CURLOPT_POSTFIELDS	=> json_encode($postData)
	));
	$response	= curl_exec($ch);
	$http_code	= curl_getinfo($ch, CURLINFO_HTTP_CODE);
	$error		= curl_error($ch);
	curl_close($ch);

	if ($response === FALSE || $http_code != 200)
	{
		echo "  USAspending $endpoint failed (HTTP $http_code) $error\n";
		return NULL;
	}
	$data	= json_decode($response, TRUE);
	return is_array($data) ? $data : NULL;
}

function discover_children($conn, $parent)
{
	$parent_id	= $parent['hv_contract_id'];
	$silent		= ($parent['hv_children_seeded'] == 0);
	$found		= 0;
	$new		= 0;
	echo "Looking for task orders under $parent_id".($silent ? " (first pass, silent)" : "")."...\n";

	for ($page = 1; $page <= 20; $page++)
	{
		$data	= usaspending_post('idvs/awards/', array(
			'award_id'	=> $parent_id,
			'type'		=> 'child_awards',
			'page'		=> $page,
			'limit'		=> 100,
			'sort'		=> 'period_of_performance_start_date',
			'order'		=> 'desc'
		));
		if ($data === NULL)
		{
			return;	//Try again next run; don't mark seeded on a partial pass
		}

		foreach ($data['results'] as $child)
		{
			$found++;
			$child_id	= $child['generated_unique_award_id'];
			$piid		= $child['piid'];
			$amount		= (float)$child['obligated_amount'];
			$child_desc	= ucfirst(strtolower(trim((string)$child['description'])));
			$label		= substr($parent['hv_contract_description']." - $piid: $child_desc", 0, 256);

			$inserted	= db_query($conn, "INSERT IGNORE INTO hv_contracts (hv_contract_id, hv_contract_description, hv_parent_id, hv_seeded) VALUES (?, ?, ?, 0)",
				array($child_id, $label, $parent_id));

			if ($inserted == 1)
			{
				$new++;
				echo "  New child: $child_id ($label)\n";
				if (!$silent)
				{
					send_alert("New task order under '".$parent['hv_contract_description']."': $piid, ".money($amount)." obligated. ".clip($child_desc, 200)
						." https://www.usaspending.gov/award/$child_id");
				}
			}
		}

		if (empty($data['page_metadata']['hasNext']))
		{
			break;
		}
	}

	echo "  $found task orders found, $new new.\n";
	db_query($conn, "UPDATE hv_contracts SET hv_children_seeded = 1 WHERE hv_contract_id = ?", array($parent_id));
}

function update_contract_mods($conn, $contract)
{
	$contract_id	= $contract['hv_contract_id'];
	$silent		= ($contract['hv_seeded'] == 0);
	$new_mods	= array();

	for ($page = 1; $page <= 10; $page++)
	{
		$data	= usaspending_post('transactions/', array(
			'award_id'	=> $contract_id,
			'page'		=> $page,
			'sort'		=> 'action_date',
			'order'		=> 'desc',
			'limit'		=> 5000
		));
		if ($data === NULL)
		{
			return;	//Leave hv_seeded alone so a failed first sync stays silent next time too
		}

		foreach ($data['results'] as $result)
		{
			$mod_no		= (string)$result['modification_number'];
			$obligation	= (float)$result['federal_action_obligation'];
			$action_date	= (string)$result['action_date'];
			$action_type	= (string)$result['action_type'];
			$action_desc	= (string)$result['action_type_description'];
			$description	= substr(trim((string)$result['description']), 0, 1024);

			$sqlstring	= "INSERT INTO hv_contract_modifications (hv_mod_id, hv_contract_id, hv_mod_obligation, hv_mod_number, hv_mod_action_type, hv_mod_action_description, hv_mod_action_date, hv_mod_description)"
					." VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
					." ON DUPLICATE KEY UPDATE hv_mod_action_date = VALUES(hv_mod_action_date), hv_mod_description = VALUES(hv_mod_description)";
			$affected	= db_query($conn, $sqlstring, array("$contract_id:$mod_no", $contract_id, $obligation, $mod_no, $action_type, $action_desc,
					$action_date !== '' ? $action_date : null, $description));

			//affected rows: 1 = inserted (new mod), 2 = existing row updated, 0 = unchanged
			if ($affected == 1)
			{
				$new_mods[]	= compact('mod_no', 'obligation', 'action_date', 'action_desc', 'description');
			}
		}

		if (empty($data['page_metadata']['hasNext']))
		{
			break;
		}
	}

	echo "  ".count($new_mods)." new modification(s)".($silent && count($new_mods) ? " loaded silently (first sync)" : "").".\n";

	if (!$silent)
	{
		foreach (array_reverse($new_mods) as $m)	//Oldest first
		{
			$label		= ucfirst(strtolower($contract['hv_contract_description']));
			$action		= ucfirst(strtolower($m['action_desc']));
			$mod_text	= ($m['mod_no'] === '' || $m['mod_no'] === '0') ? "base award" : "mod ".$m['mod_no'];
			$verb		= $m['obligation'] < 0 ? "Deobligation" : "Contract spending";
			send_alert("$verb on '$label': ".money($m['obligation'])." ($mod_text, {$m['action_date']}, $action)"
				." https://www.usaspending.gov/award/$contract_id");
		}
	}

	if ($silent)
	{
		db_query($conn, "UPDATE hv_contracts SET hv_seeded = 1 WHERE hv_contract_id = ?", array($contract_id));
	}
}

function money($amount)
{
	//$1,234.56 / -$1,234.56, with $M/$B shorthand for big numbers
	$sign	= $amount < 0 ? '-' : '';
	$a	= abs($amount);
	if ($a >= 1e9)	return $sign.'$'.number_format($a / 1e9, 2).'B';
	if ($a >= 1e6)	return $sign.'$'.number_format($a / 1e6, 2).'M';
	return $sign.'$'.number_format($a, 2);
}

function clip($s, $len)
{
	return strlen($s) > $len ? substr($s, 0, $len - 3).'...' : $s;
}

function send_alert($msg)
{
	global $quiet, $alerts_sent, $max_alerts;
	echo "  ALERT: $msg\n";
	if ($quiet)
	{
		return;
	}
	if ($alerts_sent >= $max_alerts)
	{
		echo "  (alert cap of $max_alerts reached, not sent)\n";
		return;
	}
	//dy_discord wraps the message in double quotes for the shell, so strip anything that could break out of them
	$msg	= str_replace(array('\\', '"', '`'), array('/', "'", "'"), $msg);
	$msg	= str_replace('$', '\$', $msg);	//Inside shell double quotes, \$ is a literal dollar sign
	dy_discord($msg);
	$alerts_sent++;
}

?>
