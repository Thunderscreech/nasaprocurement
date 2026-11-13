<?php
//Render an opportunity that's passed at command-line
//Set RENDER_OUT_DIR=/some/dir to test-render without touching rendered_opportunities/
error_reporting(E_ALL & ~E_DEPRECATED);
include 'common.inc';
include __DIR__.'/render_lib.inc';

$opportunity	= $argv[1] ?? '';

if(empty($opportunity))
{
	echo "Pass noticeId like 15564288faa64bb4a91eee6b026906aa to script.\n";
	exit();
}

$out_dir	= rl_out_dir();

//LEFT JOIN: NASA notices from offices not in site_names (or with no zip) still get rendered
$sqlstring	= "SELECT o.*, s.site_name FROM opportunities o LEFT JOIN site_names s ON o.officeAddressZipcode = s.officeAddressZipcode WHERE o.noticeId = ?";
$result		= db_query($conn, $sqlstring, array($opportunity));
$noticeId		= null;
$opp_type		= "";
$is_award		= false;
$render_addl_pages	= false;
$additional_pages	= "";
while ($row	= mysqli_fetch_assoc($result))
{
	$tosa			= "";
	$tags			= "";

	//rl_clean first: quickercaserupper deletes tabs, which glues words together
	$noticeId		=$row['noticeId'];
	$award_date		=$row['award_date'];
	$award_awardee		=$row['award_awardee'];
	$award_amount		=number_format((float)$row['award_amount']);
	$type			=$row['type'];
	$postedDate		="Posted: ".$row['postedDate'];
	$typeOfSetAside		=$row['typeOfSetAside'];
	$responseDeadline	="Due: ".substr((string)$row['responseDeadline'],0,10);
	$solicitationNumber	="Solicitation: ".rl_clean($row['solicitationNumber'], true);
	$title			=quickercaserupper(rl_clean($row['title'], true));
	$office			=quickercaserupper(rl_clean($row['office'], true));
	$primaryPOCfullName	=quickercaserupper(rl_clean($row['primaryPOCfullName'], true));
	$primaryPOCfullEmail	=rl_clean($row['primaryPOCfullEmail'], true);
	$primaryPOCphone	=phoneFormat(rl_clean($row['primaryPOCphone'], true));
	$secondaryPOCfullName	=quickercaserupper(rl_clean($row['secondaryPOCfullName'], true));
	$secondaryPOCfullEmail	=rl_clean($row['secondaryPOCfullEmail'], true);
	$secondaryPOCphone	=phoneFormat(rl_clean($row['secondaryPOCphone'], true));
	$site_name		=$row['site_name'];
	$description_text	=quickercaserupper(rl_clean($row['description_text']));

	//https://www.fpds.gov/help/Type_of_Set_Aside.htm
	switch($typeOfSetAside)
	{
		case "SBA";
			$tosa	= "Small Business Set-Aside";
			$tags	.= " #SBA";
			break;
		case "8AN";
			$tosa	= "Sole-Source Small Business Set-Aside";
			$tags	.= " #SBA";
			break;
		case "8A";
			$tosa	= "Small Business Set-Aside";
			break;
		case "SBP";
			$tosa	= "Partial Small Business Set-Aside";
			$tags	.= " #SBA";
			break;
		case "WOSB";
			$tosa	= "Women Owned Small Business Set-Aside";
			$tags	.= " #SBA #WOSB";
			break;

	}

	//https://dodprocurementtoolbox.com/cms/sites/default/files/resources/2020-08/SOP%20Publicizing%20Notices%20-%20FINAL.pdf
	switch($type)
	{
		case "Special Notice":
			$opp_type	= "Special Notice";
			break;
		case "Presolicitation":
			$opp_type	= "Presolicitation";
			break;
		case "Award Notice":
			$opp_type	= "Award Notice";
			$is_award	= true;
			break;
		case "Justification":
			$opp_type	= "Justification";
			break;
		case "Combined Synopsis/Solicitation":
		case "Solicitation":
			$opp_type	= "Request for Proposal";
			break;
		case "Sources Sought":
			$opp_type	= "Sources Sought";
			break;
		default:
			//Unknown/new SAM type: show it as-is rather than a blank heading
			$opp_type	= $type ? quickercaserupper($type) : "Notice";
			break;
	}

	//Contact box: up to two "Name Phone" + "(email)" pairs, skipping whatever's missing
	$contact_lines	= array();
	foreach (array(array($primaryPOCfullName, $primaryPOCphone, $primaryPOCfullEmail),
		       array($secondaryPOCfullName, $secondaryPOCphone, $secondaryPOCfullEmail)) as $poc)
	{
		$name_phone	= trim("$poc[0] $poc[1]");
		if ($name_phone !== '')	$contact_lines[] = $name_phone;
		if ($poc[2] !== '')	$contact_lines[] = "($poc[2])";
	}
	$contact_box	= implode("\n", $contact_lines);

	//Description: wrap to the body width in pixels, first page gets RL_MAIN_PAGE_LINES lines,
	//the rest is handed (already wrapped) to render_addl_pages.php via -extra.txt
	$desc_lines	= rl_trim_lines(rl_wrap($description_text, RL_FONTS."Product_Sans_Regular.ttf", RL_BODY_SIZE, RL_BODY_WIDTH));
	$main_lines	= rl_trim_lines(array_slice($desc_lines, 0, RL_MAIN_PAGE_LINES));
	$rest_lines	= rl_trim_lines(array_slice($desc_lines, RL_MAIN_PAGE_LINES));
	echo "Lines: ".count($desc_lines)."\n";

	if (count($rest_lines))
	{
		$additional_pages	= implode("\n", $rest_lines);
		$render_addl_pages	= true;
	}

	@unlink($out_dir.$noticeId.'-extra.txt');
	file_put_contents($out_dir.$noticeId.'-extra.txt',$additional_pages);

	//No continuation pages this time: clear any list left by an earlier render so stale images aren't attached
	if (!$render_addl_pages && !getenv('RENDER_OUT_DIR'))
	{
		db_query($conn, "UPDATE opportunities SET addl_pages = NULL WHERE noticeId = ?", array($noticeId));
	}
}

if(empty($noticeId))
{
	//Without this we'd render a nameless ".png"
	echo "No opportunity found for $opportunity, nothing to render.\n";
	exit(1);
}

//Begin image rendering
$template_path		= RL_DIR."templates/memo_template.png";
echo "Template: $template_path\n";

//Define our workspace, 1600/2074 to match the template
$im			= imagecreatetruecolor(1600,2074);
$template_image		= imagecreatefrompng($template_path);
$award_overlay		= imagecreatefrompng(RL_DIR."templates/awarded_template.png");

if(!$template_image)
{
	echo "Error loading template image\n";
	exit(1);
}

imagealphablending	($template_image, true);
$image_width		= imagesx($im); //Keeping this dynamic for future resize
$image_height		= imagesy($im);

//Colors used
$black 		= imagecolorallocate($im, 0, 0, 0);
$transparent 	= imagecolorallocatealpha($im, 255, 255, 255, 127);
$font		= RL_FONTS."Product_Sans_Bold.ttf";
$font_blk	= RL_FONTS."coolvetica_rg.ttf";
$times		= RL_FONTS."Product_Sans_Regular.ttf";

//Create workspace
imagealphablending	($im, false);
imagesavealpha		($im, true);
imagefilledrectangle($im, 0, 0, $image_width, $image_height, $transparent);

/*
 * Template layout (pixels), measured from memo_template.png:
 *   heading row     y  52-155, logo starts at x 1231
 *   posted | due | office row   y 156-215, columns x 52-431 | 432-867 | 868-1550
 *   title box       x 52-867,  y 216-435
 *   contact box     x 868-1550, y 216-352; set-aside row y 353-395; solicitation row y 396-435
 *   banner          y 436-505;  description box y 506-2028, x 52-1550
 *
 * Single-line slots shrink to fit (then ellipsize); shrunk text stays centered on the original midline.
 * array(text, font, size, min size, x, baseline, max width, align)
 */
$slots = array(
	array($opp_type,		$font_blk,	72, 40, 70,	128, 1130, 'left'),
	array($postedDate,		$times,		30, 18, 64,	200, 357,  'left'),
	array($responseDeadline,	$times,		30, 18, 448,	200, 409,  'left'),
	array($office,			$font_blk,	32, 18, 1209,	200, 650,  'center'),
	array($tosa,			$times,		27, 16, 1209,	388, 650,  'center'),
	array($solicitationNumber,	$times,		28, 16, 1209,	429, 650,  'center'),
);
foreach ($slots as $s)
{
	list($text, $f, $size, $min, $x, $y, $w, $align)	= $s;
	if (trim($text) === '') continue;
	list($fit_size, $fit_text)	= rl_fit_line($text, $f, $w, $size, $min);
	if ($fit_size != $size)
	{
		//Keep the shrunk text vertically centered where the full-size text sat
		list($a0)	= rl_metrics($f, $size);
		list($a1)	= rl_metrics($f, $fit_size);
		$y		-= (int)round(($a0 - $a1) / 2);
	}
	rl_draw_lines($im, array($fit_text), $f, $fit_size, $black, $x, $y, null, $align);
}

//Title: wraps inside its box, shrinking for long titles (first baseline at 34pt stays at y 263 as before)
list($asc34)				= rl_metrics($font, 34);
list($t_size, $t_lines, $t_y, $t_lh)	= rl_fit_block($title, $font, 790, 263 - $asc34, 425, 34, 18);
rl_draw_lines($im, $t_lines, $font, $t_size, $black, 65, $t_y, $t_lh);

//Contact box
list($asc22)				= rl_metrics($times, 22);
list($c_size, $c_lines, $c_y, $c_lh)	= rl_fit_block($contact_box, $times, 655, 248 - $asc22, 345, 22, 14);
rl_draw_lines($im, $c_lines, $times, $c_size, $black, 880, $c_y, $c_lh);

//Description, fixed left margin so every page lines up
rl_draw_lines($im, $main_lines, $times, RL_BODY_SIZE, $black, RL_BODY_X, 560, RL_BODY_LINE_HEIGHT);

//Overlay transparent working area on template
imagecopy($template_image, $im, 0,0,0,0,$image_width, $image_height);

if($is_award == true)
{
	echo "Award detected, putting overlay.\n";
	imagecopy($template_image, $award_overlay,0,0,0,0,$image_width, $image_height);
}

imagepng	($template_image, $out_dir."$noticeId.png");

//If there are additional pages to be rendered, spawn that now
if($render_addl_pages == true)
{
	echo "Rendering additional pages...\n";
	exec('php '.RL_DIR.'render_addl_pages.php '.escapeshellarg($noticeId));
}

echo "End of line.\n";

?>
