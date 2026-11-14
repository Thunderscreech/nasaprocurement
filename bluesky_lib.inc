<?php
/*
 * Bluesky posting for the NASA procurement bot (@nasaprocurement.bsky.social).
 *
 * Credentials come from config.ini (BSKY_HANDLE, BSKY_APP_PASSWORD; see config.example.ini).
 *
 * Image handling is deliberately forgiving: missing, unreadable or broken images
 * are skipped, oversized ones are shrunk, uploads are retried, and if the post
 * with images still fails it falls back to a text-only post.
 */

define('BSKY_PDS',             getenv('BSKY_PDS_OVERRIDE') ?: 'https://bsky.social');  // override is for testing
define('BSKY_SESSION_FILE',    getenv('BSKY_SESSION_FILE_OVERRIDE') ?: __DIR__ . '/bluesky_session.json');
define('BSKY_IMAGE_DIR',       __DIR__.'/rendered_opportunities/');
define('BSKY_MAX_IMAGE_BYTES', 976560);   // Bluesky blob limit for post images (~976 KB)
define('BSKY_MAX_IMAGES',      4);
define('BSKY_MAX_GRAPHEMES',   300);
define('BSKY_MAX_ALT',         2000);
define('BSKY_RETRIES',         3);

function bsky_log($msg)
{
	echo "[bluesky] $msg\n";
}

function bsky_load_config()
{
	include_once __DIR__ . '/config.inc';
	$cfg = array('handle' => cfg('BSKY_HANDLE'), 'app_password' => cfg('BSKY_APP_PASSWORD'));
	if (empty($cfg['handle']) || empty($cfg['app_password'])) {
		bsky_log("config.ini needs BSKY_HANDLE and BSKY_APP_PASSWORD.");
		return false;
	}
	return $cfg;
}

/*
 * Low-level XRPC call with retries on network errors, 429 and 5xx.
 * Returns ['code' => int, 'body' => array|null, 'raw' => string].
 */
function bsky_xrpc($method, $nsid, $payload = null, $jwt = null, $contentType = 'application/json')
{
	$url     = BSKY_PDS . '/xrpc/' . $nsid;
	$headers = array();
	if ($jwt)   $headers[] = 'Authorization: Bearer ' . $jwt;
	if ($payload !== null) $headers[] = 'Content-Type: ' . $contentType;

	$last = array('code' => 0, 'body' => null, 'raw' => '');
	for ($attempt = 1; $attempt <= BSKY_RETRIES; $attempt++) {
		$ch = curl_init($url);
		curl_setopt_array($ch, array(
			CURLOPT_RETURNTRANSFER => true,
			CURLOPT_CUSTOMREQUEST  => $method,
			CURLOPT_HTTPHEADER     => $headers,
			CURLOPT_CONNECTTIMEOUT => 15,
			CURLOPT_TIMEOUT        => 60,
		));
		if ($payload !== null) {
			$body = ($contentType === 'application/json') ? json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) : $payload;
			curl_setopt($ch, CURLOPT_POSTFIELDS, $body);
		} elseif ($method === 'POST') {
			curl_setopt($ch, CURLOPT_POSTFIELDS, '');  // body-less POST (refreshSession) still needs Content-Length: 0
		}
		$raw  = curl_exec($ch);
		$code = (int) curl_getinfo($ch, CURLINFO_HTTP_CODE);
		$err  = curl_error($ch);
		curl_close($ch);

		$last = array('code' => $code, 'body' => json_decode((string) $raw, true), 'raw' => (string) $raw);

		if ($code >= 200 && $code < 300) return $last;

		$retryable = ($raw === false || $code === 0 || $code === 429 || $code >= 500);
		bsky_log("$nsid attempt $attempt failed (HTTP $code" . ($err ? ", $err" : "") . "): " . substr((string) $raw, 0, 300));
		if (!$retryable || $attempt === BSKY_RETRIES) break;
		sleep(2 * $attempt);
	}
	return $last;
}

/*
 * Sessions.  A password login (createSession) is rate limited to 30 per 5 minutes / 300 per day,
 * so instead of logging in for every post the session is kept in BSKY_SESSION_FILE (chmod 600)
 * and reused across runs:
 *   - access token still valid (> 5 min left)  -> use it, no network call
 *   - expired                                  -> refreshSession with the refresh token (~2 months)
 *   - refresh fails / no cache / handle changed -> createSession (password login)
 */
function bsky_jwt_exp($jwt)
{
	$parts = explode('.', (string) $jwt);
	if (count($parts) < 2) return 0;
	$payload = json_decode(base64_decode(strtr($parts[1], '-_', '+/')), true);
	return (int) ($payload['exp'] ?? 0);
}

function bsky_session_save($session, $handle)
{
	$data = array(
		'handle'     => $handle,
		'did'        => $session['did'],
		'accessJwt'  => $session['accessJwt'],
		'refreshJwt' => $session['refreshJwt'] ?? '',
		'saved_at'   => date('c'),
	);
	$tmp = BSKY_SESSION_FILE . '.tmp' . getmypid();
	$old = umask(0077);  // tokens are as good as the password: owner-only
	$ok  = @file_put_contents($tmp, json_encode($data, JSON_PRETTY_PRINT)) !== false && @rename($tmp, BSKY_SESSION_FILE);
	umask($old);
	if (!$ok) bsky_log("Could not save session cache to " . BSKY_SESSION_FILE . " (will log in again next run).");
}

function bsky_session_forget()
{
	@unlink(BSKY_SESSION_FILE);
}

/*
 * Return a usable session (did, accessJwt, ...) or false.  $force_new skips the cached access token,
 * used when the server has just rejected it.
 */
function bsky_session($force_new = false)
{
	static $session = null;  // also reused for every post within one run
	$cfg = bsky_load_config();
	if (!$cfg) return false;

	if (!$force_new && $session && bsky_jwt_exp($session['accessJwt']) > time() + 300) return $session;

	$cache = @json_decode((string) @file_get_contents(BSKY_SESSION_FILE), true);
	if (!is_array($cache) || ($cache['handle'] ?? '') !== $cfg['handle'] || empty($cache['did'])) {
		$cache = null;  // missing, unreadable, or for a different account
	}

	// 1. Cached access token
	if ($cache && !$force_new && bsky_jwt_exp($cache['accessJwt'] ?? '') > time() + 300) {
		bsky_log("Reusing saved session.");
		return $session = $cache;
	}

	// 2. Refresh token
	if ($cache && !empty($cache['refreshJwt']) && bsky_jwt_exp($cache['refreshJwt']) > time() + 60) {
		$r = bsky_xrpc('POST', 'com.atproto.server.refreshSession', null, $cache['refreshJwt']);
		if (!empty($r['body']['accessJwt']) && !empty($r['body']['did'])) {
			bsky_log("Refreshed session.");
			bsky_session_save($r['body'], $cfg['handle']);
			return $session = $r['body'];
		}
		bsky_log("Session refresh failed (HTTP {$r['code']}); logging in with password.");
	}

	// 3. Password login
	$r = bsky_xrpc('POST', 'com.atproto.server.createSession',
		array('identifier' => $cfg['handle'], 'password' => $cfg['app_password']));
	if (empty($r['body']['accessJwt'])) {
		bsky_log("Login failed for {$cfg['handle']}.");
		//A refused password breaks every post, so page for it (once per 3 hours, see errorlog.inc)
		if (($r['code'] === 401 || $r['code'] === 403) && function_exists('sc_alert_critical')) {
			sc_alert_critical("Bluesky login refused for {$cfg['handle']} (HTTP {$r['code']}): ".($r['body']['error'] ?? '').' '.($r['body']['message'] ?? ''), array('key' => 'post-auth-bluesky'));
		}
		bsky_session_forget();
		return $session = null;
	}
	bsky_log("Logged in with password (new session).");
	bsky_session_save($r['body'], $cfg['handle']);
	return $session = $r['body'];  // has did, accessJwt, refreshJwt, handle
}

// Kept for anything else that calls it
function bsky_login()
{
	return bsky_session();
}

/*
 * Authenticated call that survives a token the server no longer accepts (revoked, clock skew):
 * on ExpiredToken/InvalidToken it gets a fresh session and tries once more.
 */
function bsky_authed_xrpc(&$session, $method, $nsid, $payload, $contentType = 'application/json')
{
	$r = bsky_xrpc($method, $nsid, $payload, $session['accessJwt'], $contentType);
	$err = $r['body']['error'] ?? '';
	if (($r['code'] === 400 || $r['code'] === 401) && in_array($err, array('ExpiredToken', 'InvalidToken', 'AuthenticationRequired'), true)) {
		bsky_log("$nsid rejected the session ($err); getting a new one and retrying.");
		$new = bsky_session(true);
		if (!$new) return $r;
		$session = $new;
		$r = bsky_xrpc($method, $nsid, $payload, $session['accessJwt'], $contentType);
	}
	return $r;
}

/*
 * Turn whatever the bot hands us (bare file names, absolute paths, comma lists,
 * blanks, duplicates) into a clean list of up to 4 existing, readable images.
 */
function bsky_resolve_images($files)
{
	if (!is_array($files)) $files = ($files === '' || $files === null) ? array() : explode(',', $files);

	$out = array();
	foreach ($files as $f) {
		$f = trim((string) $f);
		if ($f === '') continue;
		$path = ($f[0] === '/') ? $f : BSKY_IMAGE_DIR . $f;
		if (in_array($path, $out, true)) continue;
		if (!is_file($path) || !is_readable($path)) {
			bsky_log("Skipping image $path: not found or unreadable.");
			continue;
		}
		if (@getimagesize($path) === false) {
			bsky_log("Skipping image $path: not a valid image.");
			continue;
		}
		$out[] = $path;
		if (count($out) >= BSKY_MAX_IMAGES) break;
	}
	return $out;
}

/*
 * Return ['bytes', 'mime', 'width', 'height'] for an image that fits the blob
 * limit, re-encoding as JPEG (lowering quality, then scaling down) if needed.
 */
function bsky_prepare_image($path)
{
	$info = @getimagesize($path);
	if ($info === false) return false;
	list($w, $h) = $info;
	$mime  = $info['mime'];
	$bytes = @file_get_contents($path);
	if ($bytes === false) return false;

	if (strlen($bytes) <= BSKY_MAX_IMAGE_BYTES) {
		return array('bytes' => $bytes, 'mime' => $mime, 'width' => $w, 'height' => $h);
	}

	$src = @imagecreatefromstring($bytes);
	if (!$src) {
		bsky_log("Skipping image $path: too large and could not be decoded for shrinking.");
		return false;
	}
	$scale = 1.0;
	foreach (array(1.0, 0.85, 0.7, 0.55, 0.4) as $scale) {
		$nw  = max(1, (int) round($w * $scale));
		$nh  = max(1, (int) round($h * $scale));
		$dst = imagecreatetruecolor($nw, $nh);
		imagefill($dst, 0, 0, imagecolorallocate($dst, 255, 255, 255)); // flatten transparency
		imagecopyresampled($dst, $src, 0, 0, 0, 0, $nw, $nh, $w, $h);
		foreach (array(85, 70, 55) as $q) {
			ob_start();
			imagejpeg($dst, null, $q);
			$jpg = ob_get_clean();
			if (strlen($jpg) <= BSKY_MAX_IMAGE_BYTES) {
				imagedestroy($dst);
				imagedestroy($src);
				bsky_log("Shrunk $path to {$nw}x{$nh} JPEG q$q (" . strlen($jpg) . " bytes).");
				return array('bytes' => $jpg, 'mime' => 'image/jpeg', 'width' => $nw, 'height' => $nh);
			}
		}
		imagedestroy($dst);
	}
	imagedestroy($src);
	bsky_log("Skipping image $path: could not get it under the size limit.");
	return false;
}

/*
 * Build link and hashtag facets. Offsets are UTF-8 byte offsets, as Bluesky requires.
 */
function bsky_facets($text)
{
	$facets = array();

	if (preg_match_all('~https?://[^\s<>"\']+~u', $text, $m, PREG_OFFSET_CAPTURE)) {
		foreach ($m[0] as $match) {
			$url = rtrim($match[0], '.,;:!?)\'"');
			$facets[] = array(
				'index'    => array('byteStart' => $match[1], 'byteEnd' => $match[1] + strlen($url)),
				'features' => array(array('$type' => 'app.bsky.richtext.facet#link', 'uri' => $url)),
			);
		}
	}

	if (preg_match_all('~(?<![\w/#])#([A-Za-z][\w]{0,63})~u', $text, $m, PREG_OFFSET_CAPTURE)) {
		foreach ($m[0] as $i => $match) {
			$facets[] = array(
				'index'    => array('byteStart' => $match[1], 'byteEnd' => $match[1] + strlen($match[0])),
				'features' => array(array('$type' => 'app.bsky.richtext.facet#tag', 'tag' => $m[1][$i][0])),
			);
		}
	}
	return $facets;
}

/*
 * Keep the post within 300 characters while preserving the first URL and
 * everything after it: trim the text that comes before the link.
 */
function bsky_fit_text($text)
{
	$text = trim($text);
	if (mb_strlen($text) <= BSKY_MAX_GRAPHEMES) return $text;

	if (preg_match('~https?://\S+~u', $text, $m, PREG_OFFSET_CAPTURE)) {
		$cut    = mb_strlen(substr($text, 0, $m[0][1]));
		$head   = mb_substr($text, 0, $cut);
		$tail   = mb_substr($text, $cut);
		$room   = BSKY_MAX_GRAPHEMES - mb_strlen($tail) - 2; // "… "
		if ($room > 20) {
			return rtrim(mb_substr($head, 0, $room)) . '… ' . $tail;
		}
		// Tail alone is too long (huge tag list) - keep head + URL only.
		$url  = $m[0][0];
		$room = BSKY_MAX_GRAPHEMES - mb_strlen($url) - 2;
		return rtrim(mb_substr($head, 0, $room)) . '… ' . $url;
	}
	return rtrim(mb_substr($text, 0, BSKY_MAX_GRAPHEMES - 1)) . '…';
}

/*
 * Post to Bluesky. $files may be file names (relative to rendered_opportunities/),
 * absolute paths, or a comma list. Returns the post URI on success, false on failure.
 */
function postToBluesky($text, $files = array(), $alt_text = '')
{
	$session = bsky_session();
	if (!$session) return false;

	$text     = bsky_fit_text($text);
	$alt_text = mb_substr(trim(preg_replace('/\s+/u', ' ', (string) $alt_text)), 0, BSKY_MAX_ALT);

	// Upload whatever images we can; a bad image never blocks the post.
	$images = array();
	foreach (bsky_resolve_images($files) as $i => $path) {
		$img = bsky_prepare_image($path);
		if (!$img) continue;
		$r = bsky_authed_xrpc($session, 'POST', 'com.atproto.repo.uploadBlob', $img['bytes'], $img['mime']);
		if (empty($r['body']['blob'])) {
			bsky_log("Upload of $path failed; posting without it.");
			continue;
		}
		$images[] = array(
			'alt'         => ($i === 0 && $alt_text !== '') ? $alt_text : "Page " . ($i + 1) . " of the procurement notice text",
			'image'       => $r['body']['blob'],
			'aspectRatio' => array('width' => $img['width'], 'height' => $img['height']),
		);
		bsky_log("Uploaded $path.");
	}

	$record = array(
		'$type'     => 'app.bsky.feed.post',
		'text'      => $text,
		'createdAt' => gmdate('Y-m-d\TH:i:s.000\Z'),
		'langs'     => array('en'),
	);
	$facets = bsky_facets($text);
	if ($facets) $record['facets'] = $facets;
	if ($images) $record['embed'] = array('$type' => 'app.bsky.embed.images', 'images' => $images);

	$r = bsky_authed_xrpc($session, 'POST', 'com.atproto.repo.createRecord',
		array('repo' => $session['did'], 'collection' => 'app.bsky.feed.post', 'record' => $record));

	if (empty($r['body']['uri']) && $images) {
		bsky_log("Post with images failed; retrying as text only.");
		unset($record['embed']);
		$r = bsky_authed_xrpc($session, 'POST', 'com.atproto.repo.createRecord',
			array('repo' => $session['did'], 'collection' => 'app.bsky.feed.post', 'record' => $record));
	}

	if (empty($r['body']['uri'])) {
		bsky_log("Post failed: HTTP {$r['code']} " . substr($r['raw'], 0, 300));
		return false;
	}
	bsky_log("Posted {$r['body']['uri']} with " . count($images) . " image(s).");
	return $r['body']['uri'];
}
?>
