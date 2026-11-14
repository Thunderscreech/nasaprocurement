<?php
include('vendor/autoload.php');
use Abraham\TwitterOAuth\TwitterOAuth;

//Settings and secrets live in config.ini (not in git), see config.example.ini
include_once __DIR__.'/config.inc';

// Error reporting: routine problems go to logs/errors.md for the daily triage job, only critical ones to Discord
include_once __DIR__.'/errorlog.inc';

//DB connect - do it every time. PHP 8 throws on a failed connect instead of returning false: handle both
try
{
	$conn	= @mysqli_connect(cfg('DB_HOST', 'localhost'), cfg('DB_USER'), cfg('DB_PASS'));
	$__dberr= $conn ? '' : mysqli_connect_error().' (error '.mysqli_connect_errno().')';
} catch (mysqli_sql_exception $e)
{
	$conn	= false;
	$__dberr= $e->getMessage().' (error '.$e->getCode().')';
}
if (!$conn)
{
	sc_alert_critical("Cannot connect to MySQL: $__dberr", array('key' => 'db-connect', 'detail' => 'First seen in '.sc_err_script()));
	die("Error connecting to mysql\n");
}
try
{
	$__ok	= mysqli_select_db($conn, cfg('DB_NAME', 'nasaprocurement'));
} catch (mysqli_sql_exception $e)
{
	$__ok	= false;
}
if (!$__ok)
{
	sc_alert_critical('Cannot select database '.cfg('DB_NAME', 'nasaprocurement').': '.mysqli_error($conn), array('key' => 'db-select'));
	die("Error selecting DB\n");
}
unset($__dberr, $__ok);

/*
 * Run a prepared statement: values never get pasted into the SQL text, so quotes, backslashes
 * or anything else in SAM/USAspending data can't break (or inject into) the query.
 *   db_query($conn, "SELECT ... WHERE noticeId = ?", array($id))
 * Returns a mysqli_result for SELECTs, otherwise the number of affected rows.
 * Values are sent as strings (MariaDB converts for numeric columns); PHP null becomes SQL NULL.
 * Throws mysqli_sql_exception on errors, same as mysqli_query does in PHP 8.
 */
function db_query($conn, $sql, $params = array())
{
	$stmt	= mysqli_prepare($conn, $sql);
	mysqli_stmt_execute($stmt, $params ? array_values($params) : null);
	$res	= mysqli_stmt_get_result($stmt);
	$out	= ($res === false) ? mysqli_stmt_affected_rows($stmt) : $res;
	mysqli_stmt_close($stmt);
	return $out;
}

$sam_api_key	= cfg('SAM_API_KEY');

function postToMastodon($statusText, $imagePaths = []) 
{
    // Limit the number of images to 4 as Mastodon allows up to 4 images per post
    $imagePaths = array_slice($imagePaths, 0, 4);

    // Path to the toot.sh script
    $scriptPath = __DIR__.'/nasatoot.sh'; // Update this to the actual path of toot.sh

    // Escape the status text and each image path
    $escapedText = escapeshellarg($statusText);
    $escapedImages = array_map('escapeshellarg', $imagePaths);

    // Build the command string
    $command = $scriptPath . ' ' . $escapedText;
    foreach ($escapedImages as $escapedImage) {
        $command .= ' ' . $escapedImage;
    }

    // Execute the command and capture the output and status
    exec($command, $output, $status);

    // Return the result as an associative array
    return [
        'status' => $status,          // Exit status of the command
        'output' => $output           // Output from the command execution
    ];
}

function toot_curl($conn, $toot_text, $file_list)
{
	//Deprecated
	//Incorporate toot_curl.php as a function
	//Pass the toot text and an array of filepaths
	//Look up the API key from env var
	$mastodon_token	= cfg('MASTODON_TOKEN');

	$upload_root	= __DIR__.'/rendered_opportunities/';
	
	//If there are files attached, upload them and put the IDs in an array
	if(count($file_list) > 0)
	{
		//There are files
		foreach($file_list as $i => $file_name)
		{
			if(empty($file_name))
			{
				continue; //Don't bother with blanks
			}
			echo "Uploading $file_name...\n";
			$upload_curl	= "curl -X POST -H \"Authorization: Bearer $mastodon_token\" -H \"Content-Type: multipart/form-data\" https://botsin.space/api/v1/media --form file=\"@$upload_root$file_name\"";
			echo "Upload curl: $upload_curl\n";
			$return_json	= system($upload_curl);
			$return_array	= json_decode($return_json, true);
			$media_id	= $return_array['id'];
			if(strlen($media_id) <= 5)
			{
				//There is an error with the upload, alert
				dy_discord("An error occurred while attempting to upload $file_name for $toot_text");
				file_put_contents(__DIR__."/mastodon_upload_error.log", $upload_curl."\n".$return_json, FILE_APPEND);
			}

			$file_string	.= "-F \"media_ids[]=$media_id\" ";
	
		}
	}
	
	$curl_command	= "curl -X POST -H \"Authorization: Bearer $mastodon_token\" -F \"status=$toot_text\" $file_string \"https://botsin.space/api/v1/statuses\"";

	$return_curl	= system($curl_command);
	$return_array	= json_decode($return_json, true);
	$id		= $return_array['id'];

	if(isset($id))
	{
		return $id; //Return the ID or false
	} else
	{
		dy_discord("An error occurred while attempting to toot $toot_text and was logged");
		file_put_contents(__DIR__."/mastodon_error.log", $curl_command."\n".$return_json, FILE_APPEND);
	}
}

function twute($conn, $tweet = '', $mediaArray = null, $parent_tweet = null)
{
	//Revision: 7/1/2023: Updated for v2.  Uses twute for upload, not acceptable for big media yet

	echo "Tweet function executing: $tweet\n";
	print_r($mediaArray);
	echo "\n";

	//dy_discord("Attempting to tweet with twute code from SpaceTFRs");

	//This authorization model is defunct, now in ~/.twurlrc and specifying user in command

	//New method
	$acct_name	= "NASAProcurement";

	$media_string	= "";
	$reply_string	= "";

    	if(is_array($mediaArray) && !empty($mediaArray[0])) 
	{
        
        	$mediaIDS = array();
        
        	/*
            	Up to 4 media objects can be uploaded.
           	 https://developer.twitter.com/en/docs/media/upload-media/overview
        	*/
        
 		foreach($mediaArray AS $key => $media_path) 
		{
 			echo "In MediaArray: $media_path is file\n";
			if(substr($media_path, -1) == "/")
			{
				$media_path	= "";
			}

			/* Upload media to twitter API and get media ID back */
			if(strpos($media_path,'mp4'))
			{	
				echo "MP4 logic being used.\n";
//				$mediaOBJ = $connection->upload('media/upload', [
//				'media' => __DIR__.'/rendered_opportunities/'.$media_path,
//				'media_type' => 'video/mp4',
//				'media_category' => 'tweet_video',
//				], true);
			} else
			{
				echo "Catch-all media logic being used for $media_path.\n";
				if(!empty($media_path))
				{
//					$mediaOBJ = $connection->upload('media/upload', [
//					'media' => __DIR__.'/rendered_opportunities/'.$media_path
//					], true);
//					array_push($mediaIDS, $mediaOBJ->media_id_string);	
//					echo "Media String: ".$mediaOBJ->media_id_string."\n";
//					print_r($mediaOBJ);

					//Upload using Twurl
					//twurl -H "upload.twitter.com" -X POST "/1.1/media/upload.json" --file "rendered_opportunities/2213.png" --file-field "media"

					$twurl_cmd	= "twurl -H \"upload.twitter.com\" -X POST \"/1.1/media/upload.json\" --file \"".__DIR__."/rendered_opportunities/$media_path\" --file-field \"media\" -u '$acct_name'";
					echo "Twurl_cmd: $twurl_cmd\n";

					$twurl_resp	= shell_exec($twurl_cmd);
					echo "$twurl_resp\n";
					$media_id = json_decode($twurl_resp)->media_id_string;
					array_push($mediaIDS, $media_id);

					echo "Stripping blanks\n";
					foreach ($mediaIDS as $key => $value) 
					{
						if (strlen($value) < 8 || empty($value)) 
						{
							unset($mediaIDS[$key]);
						}
					}

					echo "mediaIDS array: "; 
					print_r($mediaIDS);
				}
			}
			/* push uploaded media ID to array */	

		}

		if(!empty($mediaIDS))
		{
			//$media_string	= "-d 'media_ids=" . implode(',', $mediaIDS)."'";
			
				
			// Add double quotes around each element
			$quotedArray = array_map(function($element) 
			{
				return '"' . $element . '"';
		}, $mediaIDS);


	
		//Implode the array with commas
		$imploded_media_ids = implode(',', $quotedArray);

		$media_string	= <<<EOT
, "media": {"media_ids": [$imploded_media_ids]}
EOT;
		} else
		{
			$media_string 	= "";
		}

		echo $media_string."\n";
	}

	if(!empty($parent_tweet))
	{
		//$reply_string	= " -d 'in_reply_to_status_id=$parent_tweet'";

		$reply_string	= <<<EOT
, "reply": { "in_reply_to_tweet_id": "$parent_tweet"}
EOT;

	} else
	{
		$reply_string	= "";
	}

	//Replace ampersand with something ampersandy but less breaky
	//?? is new character
	$tweet = str_replace("&", "??", $tweet);

	//dy_discord("SpaceTFRs: $tweet");

//	$post_command = "twurl -X POST -H api.twitter.com '/1.1/statuses/update.json' -d 'status=$tweet' ".$media_string." ".$reply_string." -u '$acct_name'";

	$tweet	= str_replace("\n", "\\n", $tweet);
	$tweet = str_replace("'", '\"', $tweet);

	//$tweet	= escapeshellcmd($tweet);

	$post_command	= <<<EOT
twurl -X POST -A "Content-type: application/json" -d '{"text": "$tweet" $reply_string $media_string}' "/2/tweets" -u '$acct_name'
EOT;

	echo "Twurl post: $post_command\n";

	$post_output = shell_exec($post_command);

//	echo "Twurl output: \n$post_output\n";

	if(!json_decode($post_output)->data->id)
	{
		echo "No tweet ID returned\n";
		if(str_contains(json_decode($post_output)->detail, "duplicate"))
		{
			dy_discord("Duplicate tweet: $tweet");
			return 0;
		}

		if(json_decode($post_output)->status == 429)
		{
			//We are rate limited
			//rate limit logic here
			$untweeted 	= untweeted_count($conn);
			dy_discord("Rate limited on NASAProcurement with $untweeted in the queue, snoozing for six hours");
			//insert_snooze($conn);
			//exit();
		}

		dy_discord("$acct_name tweeting error for $tweet");
	} 

	return json_decode($post_output)->data->id;

}

function untweeted_count($conn)
{
	//Return the number of jobs that are queued for tweeting
	$sqlstring	= "SELECT COUNT(*) AS untweeted FROM opportunities WHERE processed = 0";
	$untweeted	= mysqli_query($conn,$sqlstring)->fetch_object()->untweeted;
	return $untweeted;

}

function insert_snooze($conn)
{
	//Enter the current time in the snooze table
	//This causes it to not try for an hour to as to avoid spamming twitter when rate limited

	$sqlstring	= "INSERT INTO snooze (snooze_time) VALUES (NOW())";
	$result		= mysqli_query($conn, $sqlstring);

}

function check_snooze($conn)
{
	//Check if we're less than six hours from the previous snooze.  Return true if yes, false if no
	$sqlstring	= "SELECT COUNT(*) AS snooze FROM snooze WHERE snooze_time > DATE_SUB(NOW(), INTERVAL 6 HOUR)";
	$snooze_cnt	= mysqli_query($conn,$sqlstring)->fetch_object()->snooze;

	if($snooze_cnt >= 1)
	{
		return true;

	} else
	{
		return true; //false - who cares about twitter anymore
	}

}


function new_tweet($conn, $str = '', $mediaArray = null, $parent_tweet = null) 
{
	/*
		Twitter OAuth keys
		Create your App and find your API keys here:
		https://developer.twitter.com/en/apps
	*/

	$consumer_key = cfg('TWITTER_CONSUMER_KEY');
	$consumer_secret = cfg('TWITTER_CONSUMER_SECRET');
	$access_token = cfg('TWITTER_ACCESS_TOKEN');
	$access_token_secret = cfg('TWITTER_ACCESS_TOKEN_SECRET');

//	include 'test_tweet.inc'; //Uncomment this when using test account for twitter dev
    
	/* init API */
	$connection = new TwitterOAuth($consumer_key, $consumer_secret, $access_token, $access_token_secret);
    	if(is_array($mediaArray) && !empty($mediaArray[0])) 
	{
        
        	$mediaIDS = array();
        
        	/*
            	Up to 4 media objects can be uploaded.
           	 https://developer.twitter.com/en/docs/media/upload-media/overview
        	*/

		print_r($mediaArray);
        
 		foreach($mediaArray AS $key => $media_path) 
		{
 			echo "In MediaArray: $media_path is file\n";
			/* Upload media to twitter API and get media ID back */
			if(strpos($media_path,'mp4'))
			{	
				echo "MP4 logic being used.\n";
				$mediaOBJ = $connection->upload('media/upload', [
				'media' => __DIR__.'/rendered_opportunities/'.$media_path,
				'media_type' => 'video/mp4',
				'media_category' => 'tweet_video',
				], true);
			} else
			{
				echo "Catch-all media logic being used.\n";
				if(!empty($media_path))
				{
					$mediaOBJ = $connection->upload('media/upload', [
					'media' => __DIR__.'/rendered_opportunities/'.$media_path
					], true);
					array_push($mediaIDS, $mediaOBJ->media_id_string);	
				}
			}
			/* push uploaded media ID to array */	

		}
        
		/* create comma delimited list of media ID:s */	
		$mediaIDstr = implode(',', $mediaIDS);	
	}
        
	/* API params */
	$arrayCfg['status'] = $str;
	$arrayCfg['media_ids'] = $mediaIDstr;
	if(isset($parent_tweet))
	{
		$arrayCfg['in_reply_to_status_id'] = $parent_tweet;
	}
    
	/*
		Make POST request to Twitter API
		https://developer.twitter.com/en/docs/tweets/post-and-engage/api-reference/post-statuses-update
	*/
	echo "Tweeting now.\n";
	sleep(5); //Added so videos can attach correctly
	$statuses = $connection->post("statuses/update", $arrayCfg);

	if ($connection->getLastHttpCode() == 200) {
	    // Tweet posted successfully
		echo "Tweet succeeded.\n";
	} else {
	    // Handle error case
		echo "Error during tweet:".$connection->getLastHttpCode()."\n";
		var_dump($statuses);
	}

	//Log it
	$tweet		= mysqli_real_escape_string($conn, $str);

	/* return payload */
	return $statuses;
}

function quickercaserupper($textstring)
{

	//If all upper case, return ucwords
	if(strtoupper($textstring) == $textstring)
	{
		$textstring	= ucwords(strtolower($textstring));
	} else
	{
		$textstring	= $textstring;
	}

	$textstring	= str_replace('TECHNOLOGY TRANSFER OPPORTUNITY','Technology Transfer Opportunity', $textstring);
	$textstring	= str_replace('NEW PARTNERING OPPORTUNITY','Partnering Opportunity', $textstring);
	$textstring	= str_replace('_____','_', $textstring);


	$textstring	= str_replace(' It ',' IT ', $textstring);
	$textstring	= str_replace('Nasa','NASA', $textstring);
	$textstring	= str_replace('Msfc','MSFC', $textstring);
	$textstring	= str_replace('Gfsc','GFSC', $textstring);
	$textstring	= str_replace('Jsc','JSC', $textstring);

	$textstring	= str_replace("\t",' ', $textstring); //Space, not nothing: deleting tabs glued words together
	$textstring	= preg_replace('/\bJpl\b/', 'JPL', $textstring);
	$textstring	= str_replace("\u{FEFF}", '', $textstring); //Stray BOMs in SAM titles render as boxes

	return $textstring;

}

function checkForPreviousTweet($conn, $solicitationNumber)
{
	//Take a solicitationNumber and return the status-update of the previous tweet on this subject if one was made
	$twitter_status	= db_query($conn, "SELECT twitter_status FROM opportunities WHERE solicitationNumber = ? ORDER BY date_added DESC LIMIT 1", array($solicitationNumber))->fetch_object()->twitter_status ?? null;
	if(isset($twitter_status))
	{
		echo "Parent tweet found!\n";
		return $twitter_status;
	} else
	{
		return "";
	}

}

function phoneFormat($number) 
{
	if(ctype_digit($number) && strlen($number) == 10) {
  	$number = substr($number, 0, 3) .'-'. substr($number, 3, 3) .'-'. substr($number, 6);
	} else {
		if(ctype_digit($number) && strlen($number) == 7) {
			$number = substr($number, 0, 3) .'-'. substr($number, 3, 4);
		}
	}

	return $number;
}

function dy_discord($msg)
{
	//Alerts go through an external Discord bot script (DISCORD_SCRIPT in config.ini); skipped if not configured
	if (!cfg('DISCORD_SCRIPT')) { echo "(Discord not configured) $msg\n"; return; }
	exec('php '.escapeshellarg(cfg('DISCORD_SCRIPT')).' '.escapeshellarg(cfg('DISCORD_CHANNEL')).' '.escapeshellarg('<@'.cfg('DISCORD_MENTION').'> '.$msg));
}

function pushbullet($msg)
{
	$msg	= str_replace("&", "and",$msg);
	$msg	= escapeshellcmd($msg);
	$pb_token	= cfg('PUSHBULLET_TOKEN');
	$pushbullet	= <<<EOD
curl --silent -u "{$pb_token}:" -d type="note" -d body="$msg" -d title="NASA Procurement" 'https://api.pushbullet.com/v2/pushes'
EOD;
	$output		= shell_exec($pushbullet);
}
echo "Finished common.inc\n";
?>