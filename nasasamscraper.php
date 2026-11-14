<?php
//Query api.sam.gov for...  JSC zip code?  Agency names?
//Log them to database
//Another daemon will handle work with this
//https://api.sam.gov/prod/opportunities/v1/search?api_key=YOUR_KEY&zip=77058&postedFrom=03/01/2021&postedTo=03/26/2021&limit=10

include 'common.inc';
include __DIR__.'/sam_http.inc';

//Run a search for one day, then loop through results and insert into database opportunities table

$today		= date("m/d/Y");
$start_date	= date("m/d/Y", time() - 60 * 60 * 24 * 10); //previous 10 Days

//Define a base query URL then add specifics to it per search against saved_searches table
//$queryURLbase	= "https://api.sam.gov/prod/opportunities/v1/search?api_key=$sam_api_key&postedFrom=$start_date&postedTo=$today&limit=1000&";
$queryURLbase	= "https://api.sam.gov/prod/opportunities/v2/search?api_key=$sam_api_key&postedFrom=$start_date&postedTo=$today&limit=1000&";

$sqlstring	= "SELECT * FROM saved_searches";
$result		= mysqli_query($conn, $sqlstring);
$searches_ok	= 0;
$search_errors	= array();
while($row	= mysqli_fetch_assoc($result))
{
	$search_id	= $row['search_id'];
	$search_name	= $row['search_name'];
	$search_url	= $row['search_url'];

	$error		= scrape_sam($queryURLbase, $search_url, $conn, $search_id);
	if ($error === '')
	{
		$searches_ok++;
	} else
	{
		echo "Search '$search_name' failed: $error\n";
		$search_errors[]	= "$search_name: $error";
	}
}

//Track health across runs: quiet on a single blip, Discord alert if it keeps failing, and on recovery
sam_health_update(count($search_errors) == 0, implode('; ', $search_errors));

//

function scrape_sam($queryURLbase, $query, $conn, $search_id)
{
	$queryURL 	= $queryURLbase.$query;

	//Returns '' on success or a short error string (API key redacted).  On failure nothing is
	//written: no DB changes, and the last good saved response is left alone.
	$response	= sam_get_json($queryURL);

	if (!$response['ok'])
	{
		if ($response['code'] === 401 || $response['code'] === 403)
		{
			sam_auth_alert($response['code']);
		}
		return $response['error'];
	}

	$opp_array	= $response['data'];
	if (!isset($opp_array['opportunitiesData']) || !is_array($opp_array['opportunitiesData']))
	{
		//SAM sometimes answers 200 with an error object instead of results
		return sam_redact("unexpected response shape (no opportunitiesData): ".substr(json_encode($opp_array), 0, 150));
	}

	$totalresults	= $opp_array['totalRecords'] ?? count($opp_array['opportunitiesData']);
	echo "$totalresults returned\n";
	foreach ($opp_array['opportunitiesData'] as $key => $value)
	{
		$noticeId		= (string)($value['noticeId'] ?? '');
		if ($noticeId === '') continue;
		$title			= (string)($value['title'] ?? '');
		$type			= (string)($value['type'] ?? '');
		echo "Title: $title\n";
	//	if(isset($value['award']) && count($value['award']) >= 2)
		if($type == "Award Notice")
		{
			//This contract has been awarded
			//Fields are trimmed to their column sizes; strict mode rejects (and PHP 8 throws on) anything longer
			$award_date		= sam_field($value['award']['date'] ?? '', 32);
			$award_amount		= parse_award_amount($value['award']['amount'] ?? null);
			$awardee		= sam_field($value['award']['awardee']['name'] ?? '', 128);
			echo "This contract has been awarded to $awardee for ".($award_amount ?? 'unknown amount')." on $award_date\n";
		} else
		{
			//No award info
			$award_date		= '';
			$award_amount		= 0;
			$awardee		= '';
		}
		$officeAddressZipcode	= sam_field($value['officeAddress']['zipcode'] ?? '', 32);
		$officeCity		= sam_field($value['officeAddress']['city'] ?? '', 64);
	
		$placeOfPerformanceCity	= sam_field($value['placeOfPerformance']['city']['name'] ?? '', 64);
		$placeOfPerformanceState= sam_field($value['placeOfPerformance']['state']['name'] ?? '', 32);
		//The v2 API dropped 'office' and 'department'; both now live in fullParentPathName,
		//e.g. "NATIONAL AERONAUTICS AND SPACE ADMINISTRATION.NATIONAL AERONAUTICS AND SPACE ADMINISTRATION.NASA KENNEDY SPACE CENTER"
		//(first segment = department, last = office, same values v1 used to return)
		$parent_path		= array_values(array_filter(array_map('trim', explode('.', (string)($value['fullParentPathName'] ?? '')))));
		$raw_department		= $value['department'] ?? ($parent_path[0] ?? '');
		$raw_office		= $value['office'] ?? (count($parent_path) ? end($parent_path) : '');
		$office			= sam_field($raw_office, 64);
		$primaryPOCfullName	= sam_field($value['pointOfContact']['0']['fullName'] ?? '', 128);
		$primaryPOCfullEmail	= sam_field($value['pointOfContact']['0']['email'] ?? '', 255);
		$primaryPOCphone	= sam_field($value['pointOfContact']['0']['phone'] ?? '', 64);
		$secondaryPOCfullName	= sam_field($value['pointOfContact']['1']['fullName'] ?? '', 128);
		$secondaryPOCfullEmail	= sam_field($value['pointOfContact']['1']['email'] ?? '', 255);
		$secondaryPOCphone	= sam_field($value['pointOfContact']['1']['phone'] ?? '', 64);
	
		$typeOfSetAside		= sam_field($value['typeOfSetAside'] ?? '', 32);
		$department		= sam_field($raw_department, 128);
		$description		= (string)($value['description'] ?? '');
		$postedDate		= (string)($value['postedDate'] ?? '');
		$responseDeadline	= (string)($value['responseDeadLine'] ?? '');
		//	$solicitation		= $value['solicitation'];
		$solicitationNumber	= (string)($value['solicitationNumber'] ?? '');
	
		$uiLink			= (string)($value['uiLink'] ?? '');
	
	//	echo "$noticeId  - $type - $department - $description - $postedDate - $responseDeadline - $solicitationNumber - $title - $uiLink\n";
	
		$sqlstring		 = "INSERT INTO opportunities (noticeId, award_date, award_awardee, award_amount, type, department, description, postedDate, typeOfSetAside, responseDeadline, solicitationNumber, title, uilink, officeAddressZipcode, city, placeOfPerformanceCity, placeOfPerformanceState, office, primaryPOCfullName, primaryPOCfullEmail, primaryPOCphone, secondaryPOCfullName, secondaryPOCfullEmail, secondaryPOCphone
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
		) ON DUPLICATE KEY UPDATE award_date = VALUES(award_date), award_awardee = VALUES(award_awardee), award_amount = VALUES(award_amount),
		  city = VALUES(city), office = VALUES(office), department = VALUES(department)";
		$params			= array($noticeId, $award_date, $awardee, $award_amount, $type, $department, $description, $postedDate, $typeOfSetAside,
						$responseDeadline, $solicitationNumber, $title, $uiLink, $officeAddressZipcode, $officeCity, $placeOfPerformanceCity,
						$placeOfPerformanceState, $office, $primaryPOCfullName, $primaryPOCfullEmail, $primaryPOCphone,
						$secondaryPOCfullName, $secondaryPOCfullEmail, $secondaryPOCphone);

		echo "Saving $noticeId ($type, $office)\n";
		//One bad notice must not kill the run (PHP 8 mysqli throws on any SQL error)
		try
		{
			db_query($conn, $sqlstring, $params);
		} catch (mysqli_sql_exception $e)
		{
			echo "SQL error for $noticeId, skipping: ".$e->getMessage()."\n";
			report_insert_error($noticeId, $e->getMessage());
			continue;
		}

	
		//Get the description text
		exec('php '.escapeshellarg(__DIR__.'/get_description.php').' '.escapeshellarg($noticeId).' > '.escapeshellarg(__DIR__.'/get_description.log').' 2>&1');
	
	}
	
	//Save the last good response for debugging (own folder: federal_opportunities/ has its own N.json files)
	$save_dir	= __DIR__.'/sam_responses';
	if (!is_dir($save_dir)) @mkdir($save_dir, 0755);
	sam_save_atomic("$save_dir/$search_id.json", json_encode($opp_array, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE));

	return '';
}

function sam_field($value, $max_len)
{
	//SAM leaves optional fields out (no 2nd contact, no place of performance...).  Missing -> '',
	//trimmed to the column size (strict mode rejects longer values).  Bound as a parameter, so no escaping.
	return mb_substr(trim((string)$value), 0, $max_len);
}

function parse_award_amount($raw)
{
	//SAM sends '131209.38', '0.00', '20625', sometimes nothing.  Returns a decimal string, or null (stored as NULL).
	$clean	= preg_replace('/[^0-9.\-]/', '', (string)$raw);	//Tolerate "$1,234.56"
	if ($clean === '' || !is_numeric($clean) || abs((float)$clean) >= 1e13)	//DECIMAL(15,2) limit
	{
		if ($raw !== null && $raw !== '') echo "Unusable award amount '$raw', storing NULL\n";
		return null;
	}
	return sprintf('%.2f', (float)$clean);
}

function report_insert_error($noticeId, $error)
{
	//Alert once per notice, not every 20 minutes for the 10 days it stays in the search window
	$seen_file	= __DIR__.'/scraper_errors.seen';
	$seen		= @file($seen_file, FILE_IGNORE_NEW_LINES) ?: array();
	if (in_array($noticeId, $seen)) return;
	file_put_contents($seen_file, "$noticeId\n", FILE_APPEND);
	$error		= preg_replace('/[^A-Za-z0-9 _.,:()\'-]/', ' ', substr($error, 0, 150));	//keep the logged message plain
	sc_log_error("NASAProcurement scraper could not save notice $noticeId: $error");
}

//Update last run - only when at least one search actually came back, so a stale last_run means trouble
if ($searches_ok > 0)
{
	$sqlstring	= "UPDATE control_data SET control_data = NOW() WHERE control_name = 'last_run'";
	$result		= mysqli_query($conn, $sqlstring);
}

?>