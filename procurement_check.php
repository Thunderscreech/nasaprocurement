<?php
//Includes
include 'common.inc';
include __DIR__ . '/bluesky_lib.inc';

//Check if I'm supposed to be running
$sqlstring	= "SELECT control_data FROM control_data WHERE control_name = 'active'";
$active		= mysqli_query($conn,$sqlstring)->fetch_object()->control_data;
if($active == '1')
{
	//No action required, we are on like kong
} else
{
	echo "According to status.status_item.active, I am supposed to be asleep.  Exiting...\n";
	exit();
}




//Test mode
if(isset($argv[1]))
{
	$test_mode	= true;
	echo "Running in test mode.\n";
} else
{
	$test_mode	= false;
	echo "Running in real mode.\n";
}

//processed: 0 = queued, 1 = posted everywhere, 2 = skipped (stale backlog), 3 = gave up after $max_attempts
$max_attempts	= 3;

//Query for new opportunities.  A notice qualifies if its office zip is a known site, OR it's a NASA notice
//from an office we don't have in site_names (empty zip, new office...) - the NAICS/ccode searches also pull
//in non-NASA notices (Space Force, AFRL, etc.) and those are still filtered out by the department check.
$site_join	= "FROM opportunities o LEFT JOIN site_names s ON o.officeAddressZipcode = s.officeAddressZipcode
		   WHERE (s.site_name IS NOT NULL OR o.department LIKE 'NATIONAL AERONAUTICS%')";
if($test_mode == true)
{
	$sqlstring	= "SELECT o.*, s.site_name $site_join";
} else
{
	$sqlstring	= "SELECT o.*, s.site_name $site_join AND o.processed = 0 ORDER BY o.date_added LIMIT 5";
}

function site_name_for($row)
{
	//Prefer the friendly name from site_names, fall back to the office name SAM gives us
	if(!empty($row['site_name']))
	{
		return $row['site_name'];
	}
	$office	= trim((string)$row['office']);
	if($office === '' || strtoupper($office) === 'NATIONAL AERONAUTICS AND SPACE ADMINISTRATION')
	{
		return "NASA";
	}
	return quickercaserupper($office);
}

$result		= mysqli_query($conn, $sqlstring);
$num_rows 	= mysqli_num_rows($result);
echo "$num_rows rows returned.\n";
while ($row	= mysqli_fetch_assoc($result))
{
	$tosa			= "";
	$tags			= "";
	$message		= "";

	$noticeId		=$row['noticeId'];
	$award_date		=$row['award_date'];
	$award_awardee		=$row['award_awardee'];
	$award_amount		=(float)$row['award_amount'] > 0 ? number_format((float)$row['award_amount']) : '';	//NULL/0 = SAM gave no usable amount
	$type			=$row['type'];
	$department		=quickercaserupper($row['department']);
	$description		=$row['description'];
	$postedDate		=$row['postedDate'];
	$typeOfSetAside		=$row['typeOfSetAside'];
	$responseDeadline	=$row['responseDeadline'];
	$solicitationNumber	=$row['solicitationNumber'];
	$title			=quickercaserupper($row['title']);
	$uilink			=str_replace('://beta.sam.gov', '://sam.gov', (string)$row['uiLink']);	//Old rows have beta.sam.gov links
	$officeAddressZipcode	=$row['officeAddressZipcode'];
	$city			=$row['city'];
	$placeOfPerformanceCity	=quickercaserupper($row['placeOfPerformanceCity']);
	$placeOfPerformanceState=$row['placeOfPerformanceState'];
	$office			=quickercaserupper($row['office']);
	$primaryPOCfullName	=quickercaserupper($row['primaryPOCfullName']);
	$primaryPOCfullEmail	=$row['primaryPOCfullEmail'];
	$primaryPOCphone	=$row['primaryPOCphone'];
	$secondaryPOCfullName	=quickercaserupper($row['secondaryPOCfullName']);
	$secondaryPOCfullEmail	=$row['secondaryPOCfullEmail'];
	$secondaryPOCphone	=$row['secondaryPOCphone'];
	$site_name		=site_name_for($row);
	$description_text	=$row['description_text'];


	//https://www.fpds.gov/help/Type_of_Set_Aside.htm
	switch($typeOfSetAside)
	{
		case "SBA";
			$tosa	= "Small Business Set-Aside per FAR 19.5.";
			$tags	.= " #SBA";
			break;
		case "8AN";
			$tosa	= "Sole-Source Small Business Set-Aside per FAR 19.8.";
			$tags	.= " #SBA";
			break;
		case "8A";
			$tosa	= "Small Business Set-Aside per FAR 19.805.";
			break;
		case "SBP";
			$tosa	= "Partial Small Business Set-Aside per FAR 19.502.";
			$tags	.= " #SBA";
			break;
		case "WOSB";
			$tosa	= "Women Owned Small Business Set-Aside per FAR 19.15.";
			$tags	.= " #SBA #WOSB";
			break;

	}

	//https://dodprocurementtoolbox.com/cms/sites/default/files/resources/2020-08/SOP%20Publicizing%20Notices%20-%20FINAL.pdf
	switch($type)
	{
		case "Special Notice":

			$message	= "$site_name has a special notice regarding '$title'. See: $uilink";
			break;
		case "Presolicitation":
			$message	= "$site_name requests information from industry regarding '$title' for a possible future bid. See: $uilink";
			break;
		case "Award Notice":
			$message	= "$site_name has awarded the '$title' contract to $award_awardee".($award_amount !== '' ? " for \$$award_amount" : "").". See: $uilink";
			break;
		case "Justification":
			$message	= "$site_name has posted a justification regarding the '$title' contract, see $uilink for more information.";
			break;
		case "Combined Synopsis/Solicitation":
			$message	= "$site_name has released a combined synopsis/solicitation to expedite bids for '$title'. See: $uilink";
			break;
		case "Solicitation":
			$message	= "$site_name has issued an RFP for '$title'. See: $uilink";
			break;
		case "Sources Sought":
			$message	= "$site_name is soliciting interest re: '$title'. See: $uilink";
			break;
		default:
			//New/rare SAM types (Sale of Surplus Property, Intent to Bundle, ...) - never reuse the previous row's text
			$message	= "$site_name has posted a ".($type ? "'$type' notice" : "notice")." regarding '$title'. See: $uilink";
			break;
	}

	$tweet		= "$message $tosa$tags";

	//Colors: https://i.stack.imgur.com/HFSl1.png

	if(strlen($tweet) >= 300)
	{
		$color_code	= "\e[31m";
	} else
	{
		$color_code	= "\e[39m";
	}

	if($test_mode != true)
	{
		//No description text yet (the scraper's fetch failed or hit the API limit)?  Try once more now,
		//and give a brand-new notice up to an hour to get it rather than posting it image-less forever.
		if(empty($description_text))
		{
			exec('php '.escapeshellarg(__DIR__.'/get_description.php').' '.escapeshellarg($noticeId).' > /dev/null 2>&1');
			$description_text	= (string)db_query($conn, "SELECT description_text FROM opportunities WHERE noticeId = ?", array($noticeId))->fetch_object()->description_text;
			if(empty($description_text) && strtotime($row['date_added']) > time() - 3600)
			{
				echo "No description text for $noticeId yet, will try again next run.\n";
				continue;
			}
		}

		//Render graphic
		if(!empty($description_text))
		{
			exec('php '.escapeshellarg(__DIR__.'/render_opportunity.php').' '.escapeshellarg($noticeId));	//Synchronous - no wait needed

			unset($file_path);

			if(file_exists(__DIR__.'/rendered_opportunities/'.$noticeId.'.png'))
			{
				$file_path[]	= $noticeId.".png";
			} else
			{
				$file_path[]	= "";
			}
			$subresult	= db_query($conn, "SELECT addl_pages FROM opportunities WHERE noticeId = ?", array($noticeId));
			while($subrow	= mysqli_fetch_assoc($subresult))
			{
				$addl_pages	= $subrow['addl_pages'];	
				echo "Additional pages: $addl_pages\n";	
			}
			$addl_pages	= explode(',',(string)$addl_pages);
			$file_path	= array_merge($file_path,$addl_pages);

		} else
		{
			//We ran out of API calls or something and don't have text for it.  Stick to the basics
			$file_path = array();
		}

		//Turn bare file names into absolute paths and drop anything missing/blank/broken,
		//so a bad image never takes the whole post down (cron's cwd isn't this folder)
		$file_path	= bsky_resolve_images($file_path);
		echo "Images to attach: ".implode(', ', $file_path)."\n";

		//If we've tweeted about this solicitationNumber previously, let's attach our tweet to the most recent
		//$parent_status	= checkForPreviousTweet($conn, $solicitationNumber);

		//Tweet new results

		//$parent_status	= ""; //We weren't previously using this, we are now.
		//$return_val	= new_tweet($conn, $tweet, $file_path, $parent_status);

		//$tweet		= escapeshellarg($tweet);

		//$return_val	= twute($conn, $tweet, $file_path, $parent_status);	
//		$return_val	= twute($conn, $tweet, array("/home/chairboy/tfrmonitor/rendered_maps/temp_file.png"), $parent_status);

		//$tweet_id	= $return_val->id;	//We'll track status updates so we can respond to existing threads with weather and stuff
		//$tweet_id	= $return_val;
		//Each platform is tracked separately so a retry never double-posts to the one that already worked.
		//The notice is only marked processed once both have succeeded (or we've given up).

		//Post to Mastodon
		$mastodon_done	= !empty($row['mastodon_status']);
		if(!$mastodon_done)
		{
			$toot_result = postToMastodon($tweet, $file_path);
			if ($toot_result['status'] === 0)
			{
				//nasatoot.sh ends with "Response: {status json}"; keep the toot ID if we can find it
				$toot_id	= "posted";
				foreach ($toot_result['output'] as $line)
				{
					if (strpos($line, 'Response: ') === 0)
					{
						$resp	= json_decode(substr($line, 10), true);
						if (!empty($resp['id'])) $toot_id = substr((string)$resp['id'], 0, 32);
					}
				}
				db_query($conn, "UPDATE opportunities SET mastodon_status = ? WHERE noticeId = ?", array($toot_id, $noticeId));
				$mastodon_done	= true;
				echo "Toot posted successfully ($toot_id).\n";
			} else
			{
				echo "Failed to post toot. Output:\n";
				print_r($toot_result['output']);
				//nasatoot.sh prints "... failed with status 401": a rejected token breaks every post, so page for it
				if (preg_match('/failed with status (401|403)\b/', implode("\n", $toot_result['output']), $m))
				{
					sc_alert_critical("Mastodon rejected the bot's access token (HTTP {$m[1]})", array('key' => 'post-auth-mastodon'));
				}
			}
		} else
		{
			echo "Already on Mastodon ({$row['mastodon_status']}), skipping.\n";
		}

		//Post to Bluesky (@nasaprocurement.bsky.social)
		$bluesky_done	= !empty($row['bluesky_status']);
		if(!$bluesky_done)
		{
			$bsky_alt	= "Memo-style rendering of the SAM.gov notice '$title' from $site_name. ".(string)$description_text;
			$bsky_uri	= postToBluesky($tweet, $file_path, $bsky_alt);
			if ($bsky_uri)
			{
				db_query($conn, "UPDATE opportunities SET bluesky_status = ? WHERE noticeId = ?", array(substr($bsky_uri, 0, 128), $noticeId));
				$bluesky_done	= true;
				echo "Bluesky post: $bsky_uri\n";
			} else
			{
				echo "Bluesky post failed for $noticeId.\n";
			}
		} else
		{
			echo "Already on Bluesky ({$row['bluesky_status']}), skipping.\n";
		}

		if($mastodon_done && $bluesky_done)
		{
			db_query($conn, "UPDATE opportunities SET processed = 1 WHERE noticeId = ?", array($noticeId));
			echo "Marked $noticeId processed.\n";
		} else
		{
			$attempts	= (int)$row['post_attempts'] + 1;
			$give_up	= ($attempts >= $max_attempts);
			db_query($conn, "UPDATE opportunities SET post_attempts = ?".($give_up ? ", processed = 3" : "")." WHERE noticeId = ?", array($attempts, $noticeId));
			$failed		= trim(($mastodon_done ? "" : "Mastodon ").($bluesky_done ? "" : "Bluesky"));
			if($give_up)
			{
				echo "Giving up on $noticeId after $attempts attempts ($failed failed).\n";
				sc_log_error("NASAProcurement gave up posting $noticeId to $failed after $attempts attempts");
			} else
			{
				echo "Attempt $attempts of $max_attempts for $noticeId failed on $failed; will retry next run.\n";
			}
		}

		//$toot_id	= toot_curl($conn, $tweet, $file_path);
		//$sqlstring	= "UPDATE opportunities SET mastodon_status = '$toot_id' WHERE noticeId = '$noticeId'";
		//$subresult	= mysqli_query($conn, $sqlstring);

		sleep(5);

	} else
	{
		echo "T: ($color_code".strlen($tweet)."\e[39m)$tweet\n";
	}
}

//Update last processed
$sqlstring	= "UPDATE control_data SET control_data = NOW() WHERE control_name = 'last_process'";
$result		= mysqli_query($conn, $sqlstring);



?>