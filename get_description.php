<?php
//Fetch a notice's full description text from SAM.gov (costs one API call) and store it.
//Called by nasasamscraper.php per notice and by procurement_check.php when text is missing.
//On any failure nothing is written, so description_text stays empty and gets retried later.

include 'common.inc';
include __DIR__.'/sam_http.inc';

$noticeId	= $argv[1] ?? '';
if ($noticeId === '')
{
	echo "Usage: php get_description.php <noticeId>\n";
	exit(1);
}
$row	= db_query($conn, "SELECT description, description_text FROM opportunities WHERE noticeId = ?", array($noticeId))->fetch_assoc();
if (!$row)
{
	echo "No opportunity $noticeId in the database.\n";
	exit(1);
}

if (strlen((string)$row['description_text']) > 0)
{
	//We have the text already and no action is required
	echo "No action required, text already downloaded.\n";
	exit(0);
}

$description_url	= (string)$row['description'];
if (!preg_match('#^https://#', $description_url))
{
	echo "No description URL for $noticeId, nothing to fetch.\n";
	exit(0);
}

//We need to spend an API call on getting the text
echo "Getting description_text for $noticeId.  URL: ".sam_redact($description_url)."\n";
$response	= sam_get_json($description_url."&api_key=$sam_api_key");

if (!$response['ok'])
{
	echo "Fetch failed for $noticeId ({$response['error']}); will retry on a later run.\n";
	if ($response['code'] === 401 || $response['code'] === 403)
	{
		sam_auth_alert($response['code']);
	}
	exit(1);
}

if (!array_key_exists('description', $response['data']))
{
	echo "Response for $noticeId has no description field; will retry on a later run.\n";
	exit(1);
}

//Keep the last response around for debugging
sam_save_atomic(__DIR__.'/description.json', json_encode($response['data'], JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE));

$d_text		= strip_tags(html_entity_decode((string)$response['data']['description']));
echo "Text:\n$d_text\n";

//Update the database with the text
db_query($conn, "UPDATE opportunities SET description_text = ? WHERE noticeId = ?", array($d_text, $noticeId));
?>
