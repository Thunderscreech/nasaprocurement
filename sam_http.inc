<?php
/*
 * SAM.gov HTTP helper shared by nasasamscraper.php and get_description.php.
 *
 * Replaces bare file_get_contents(): explicit timeouts, retries for transient failures
 * (network errors, 5xx), and a clear result instead of false + warnings.  429 (out of API
 * quota) and 4xx are not retried - retrying can't fix them and only burns time.
 */

define('SAM_CONNECT_TIMEOUT',	15);
define('SAM_TIMEOUT',		90);
define('SAM_RETRIES',		3);

/*
 * GET a SAM.gov URL.  Returns array:
 *   ok    true only for HTTP 2xx with a body that decodes as JSON
 *   code  HTTP status (0 = no response at all)
 *   data  decoded JSON array (or null)
 *   error short human-readable reason when !ok (never contains the API key)
 */
function sam_get_json($url)
{
	$last	= array('ok' => false, 'code' => 0, 'data' => null, 'error' => 'not attempted');
	for ($attempt = 1; $attempt <= SAM_RETRIES; $attempt++)
	{
		$ch	= curl_init($url);
		curl_setopt_array($ch, array(
			CURLOPT_RETURNTRANSFER	=> true,
			CURLOPT_FOLLOWLOCATION	=> true,
			CURLOPT_MAXREDIRS	=> 3,
			CURLOPT_CONNECTTIMEOUT	=> SAM_CONNECT_TIMEOUT,
			CURLOPT_TIMEOUT		=> SAM_TIMEOUT,
			CURLOPT_HTTPHEADER	=> array('Accept: application/json'),
		));
		$body	= curl_exec($ch);
		$code	= (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
		$cerr	= curl_error($ch);
		curl_close($ch);

		if ($body === false || $code === 0)
		{
			$last	= array('ok' => false, 'code' => 0, 'data' => null, 'error' => "network error: $cerr");
		} else
		{
			$data	= json_decode($body, true);
			if ($code >= 200 && $code < 300 && is_array($data))
			{
				return array('ok' => true, 'code' => $code, 'data' => $data, 'error' => '');
			}
			$reason	= ($code >= 200 && $code < 300) ? "HTTP $code but the response was not valid JSON" : "HTTP $code";
			//SAM error bodies look like {"error":{"code":"OVER_RATE_LIMIT","message":"..."}} or {"message":"..."}
			if (is_array($data))
			{
				$msg	= $data['error']['message'] ?? $data['error']['code'] ?? $data['message'] ?? $data['errorMessage'] ?? null;
				if (is_string($msg)) $reason .= ": ".substr($msg, 0, 150);
			}
			$last	= array('ok' => false, 'code' => $code, 'data' => is_array($data) ? $data : null, 'error' => $reason);

			$transient	= ($code >= 500 || ($code >= 200 && $code < 300));	//5xx, or a truncated/garbled 2xx body
			if (!$transient) break;
		}

		if ($attempt < SAM_RETRIES)
		{
			echo "SAM request attempt $attempt failed ({$last['error']}), retrying...\n";
			sleep(5 * $attempt);
		}
	}
	$last['error']	= sam_redact($last['error']);
	return $last;
}

//Never let the API key reach logs or Discord
function sam_redact($text)
{
	global $sam_api_key;
	$text	= preg_replace('/api_key=[^&\s"]+/', 'api_key=REDACTED', (string)$text);
	return empty($sam_api_key) ? $text : str_replace($sam_api_key, 'REDACTED', $text);
}

//Write a file via temp + rename so a crash or full disk never leaves a half-written/empty file in place
function sam_save_atomic($path, $contents)
{
	$tmp	= $path.'.tmp'.getmypid();
	if (@file_put_contents($tmp, $contents) === false) return false;
	return @rename($tmp, $path);
}

/*
 * Run health, persisted between cron runs, so one blip stays quiet but an outage gets reported:
 * alert after SAM_ALERT_AFTER consecutive failed runs, once, then once more when it recovers.
 */
define('SAM_ALERT_AFTER',	3);	//Scraper runs every 20 min, so ~1 hour of failures

function sam_health_update($ok, $error = '')
{
	$file	= __DIR__.'/scraper_health.json';
	$h	= @json_decode((string)@file_get_contents($file), true);
	if (!is_array($h)) $h = array('consecutive_failures' => 0, 'alerted' => false, 'last_error' => '', 'auth_alerted_at' => 0);

	if ($ok)
	{
		if (!empty($h['alerted']))
		{
			dy_discord("NASAProcurement scraper recovered: SAM.gov searches are working again after {$h['consecutive_failures']} failed run(s)");
		}
		$h['consecutive_failures']	= 0;
		$h['alerted']			= false;
		$h['last_error']		= '';
	} else
	{
		$h['consecutive_failures']++;
		$h['last_error']		= $error;
		echo "Run failed ({$h['consecutive_failures']} in a row): $error\n";
		if ($h['consecutive_failures'] >= SAM_ALERT_AFTER && empty($h['alerted']))
		{
			dy_discord("NASAProcurement scraper: SAM.gov searches have failed {$h['consecutive_failures']} runs in a row. Last error: ".sam_shell_safe($error));
			$h['alerted']	= true;
		}
	}
	sam_save_atomic($file, json_encode($h, JSON_PRETTY_PRINT));
}

//Auth failures need a human (new API key), so alert right away - but at most once a day
function sam_auth_alert($code)
{
	$file	= __DIR__.'/scraper_health.json';
	$h	= @json_decode((string)@file_get_contents($file), true);
	if (!is_array($h)) $h = array('consecutive_failures' => 0, 'alerted' => false, 'last_error' => '', 'auth_alerted_at' => 0);
	if (time() - (int)($h['auth_alerted_at'] ?? 0) < 86400) return;
	dy_discord("NASAProcurement: SAM.gov API key expired or invalid (HTTP $code). Scraper can't fetch anything until the key in common.inc is replaced.");
	$h['auth_alerted_at']	= time();
	sam_save_atomic($file, json_encode($h, JSON_PRETTY_PRINT));
}

//dy_discord wraps its message in shell double quotes
function sam_shell_safe($text)
{
	return preg_replace('/[^A-Za-z0-9 _.,:;()\'\/=-]/', ' ', substr((string)$text, 0, 200));
}
?>
