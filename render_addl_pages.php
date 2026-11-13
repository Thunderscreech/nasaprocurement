<?php
//Turn rendered_opportunities/$noticeId-extra.txt into continuation pages
//The text arrives already wrapped to the body width by render_opportunity.php (one line per line),
//so this only paginates.  The file list is saved to the DB so the pages get posted together.
//Set RENDER_OUT_DIR=/some/dir to test-render without touching rendered_opportunities/

error_reporting(E_ALL & ~E_DEPRECATED);
include 'common.inc';
include __DIR__.'/render_lib.inc';

$noticeId	= $argv[1] ?? '';
$out_dir	= rl_out_dir();

//Load the text into memory
$working_text	= @file_get_contents($out_dir.$noticeId.'-extra.txt');
if(empty($noticeId) || empty(trim((string)$working_text)))
{
	echo "Error loading working text.\n";
	exit();
}

$lines		= rl_trim_lines(explode("\n", $working_text));
$per_page	= RL_ADDL_PAGE_LINES;
$file_list	= array();

for ($page = 1; $page <= RL_MAX_ADDL_PAGES && count($lines); $page++)
{
	$page_lines	= rl_trim_lines(array_slice($lines, 0, $per_page));
	$lines		= rl_trim_lines(array_slice($lines, $per_page));

	if ($page == RL_MAX_ADDL_PAGES && count($lines))
	{
		//Out of image slots: make room for a note on the last page
		$page_lines	= rl_trim_lines(array_slice($page_lines, 0, $per_page - 2));
		$page_lines[]	= "";
		$page_lines[]	= "[Text continues - see the SAM.gov link in the post for the full notice]";
		echo "We hit maximum pages, ending early.\n";
	}

	if (!count($page_lines)) break;

	$file_name	= "$noticeId-$page.png";
	render_page($page_lines, $out_dir.$file_name);
	$file_list[]	= $file_name;
}

//Put the list of generated files into the database
$file_list	= implode(',', $file_list);
echo "File list: $file_list\n";
if (!getenv('RENDER_OUT_DIR'))	//Test renders don't touch the DB
{
	db_query($conn, "UPDATE opportunities SET addl_pages = ? WHERE noticeId = ?", array($file_list, $noticeId));
}

//Take a page's worth of (pre-wrapped) lines and render it to an image
function render_page($page_lines, $path)
{
	$template_image		= imagecreatefrompng(RL_DIR."templates/memo_addl_page_template.png");
	imagealphablending	($template_image, true);

	//Define our workspace, 1600/2074 to match the template
	$im			= imagecreatetruecolor(1600,2074);
	$image_width		= imagesx($im);
	$image_height		= imagesy($im);

	$black 		= imagecolorallocate($im, 0, 0, 0);
	$transparent 	= imagecolorallocatealpha($im, 255, 255, 255, 127);
	$times		= RL_FONTS."Product_Sans_Regular.ttf";

	//Create workspace
	imagealphablending	($im, false);
	imagesavealpha		($im, true);
	imagefilledrectangle($im, 0, 0, $image_width, $image_height, $transparent);

	//Same left margin, size and spacing as the first page's description
	rl_draw_lines($im, $page_lines, $times, RL_BODY_SIZE, $black, RL_BODY_X, 240, RL_BODY_LINE_HEIGHT);

	//Overlay transparent working area on template
	imagecopy($template_image, $im, 0,0,0,0,$image_width, $image_height);
	imagepng	($template_image, $path);
	echo "Rendered $path\n";
}

?>
