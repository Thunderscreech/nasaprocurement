<?php
/*
 * Text layout helpers for the memo renderers (render_opportunity.php, render_addl_pages.php).
 *
 * Everything here measures in pixels with the actual font instead of counting characters,
 * so wide letters, long URLs/part numbers and odd Unicode from SAM.gov can't run past the
 * template's boxes.
 */

define('RL_DIR',	__DIR__.'/');
define('RL_FONTS',	RL_DIR.'fonts/');

//Where PNGs and -extra.txt files go.  RENDER_OUT_DIR lets you test-render somewhere harmless.
function rl_out_dir()
{
	$dir	= getenv('RENDER_OUT_DIR');
	return $dir ? rtrim($dir, '/').'/' : RL_DIR.'rendered_opportunities/';
}

//Line spacing used for all multi-line text (GD's own spacing for these fonts is ~1.4x the point size)
define('RL_LINE_SPACING', 1.4);

//Description body: both templates have the same body box (x 52..1550), so pages wrap identically
define('RL_BODY_X',		111);
define('RL_BODY_WIDTH',		1380);
define('RL_BODY_SIZE',		32);
define('RL_BODY_LINE_HEIGHT',	45);
define('RL_MAIN_PAGE_LINES',	32);	//Lines that fit in the description box on the first page
define('RL_ADDL_PAGE_LINES',	39);	//Lines that fit on a continuation page
define('RL_MAX_ADDL_PAGES',	3);	//1 memo + 3 continuation pages = the 4-image limit on Mastodon/Bluesky

/*
 * Code points Product Sans can draw (from fc-query).  It's the narrowest of our fonts, so text
 * cleaned to this set renders on every font we use.
 */
function rl_glyph_ok($cp)
{
	static $ranges = array(
		array(0x20, 0x7e), array(0xa0, 0xff), array(0x131, 0x131), array(0x152, 0x153),
		array(0x2c6, 0x2c6), array(0x2da, 0x2da), array(0x2dc, 0x2dc), array(0x2013, 0x2014),
		array(0x2018, 0x201a), array(0x201c, 0x201e), array(0x2022, 0x2022), array(0x2026, 0x2026),
		array(0x2039, 0x203a), array(0x2044, 0x2044), array(0x2074, 0x2074), array(0x20ac, 0x20ac),
		array(0x2212, 0x2212), array(0x2215, 0x2215),
	);
	foreach ($ranges as $r)
	{
		if ($cp >= $r[0] && $cp <= $r[1]) return true;
	}
	return false;
}

/*
 * Normalize SAM.gov text for rendering: decode leftover HTML entities, turn tabs and odd spaces
 * into plain spaces, swap characters the font lacks for look-alikes, drop invisible/control
 * characters, and tidy blank lines.  Keeps line breaks unless $single_line is true.
 */
function rl_clean($text, $single_line = false)
{
	$text	= (string)$text;
	if ($text === '') return '';

	//Some descriptions are double-encoded (&amp;nbsp;)
	for ($i = 0; $i < 2; $i++)
	{
		$decoded = html_entity_decode($text, ENT_QUOTES | ENT_HTML5, 'UTF-8');
		if ($decoded === $text) break;
		$text	= $decoded;
	}

	if (!mb_check_encoding($text, 'UTF-8'))
	{
		$text	= mb_convert_encoding($text, 'UTF-8', 'Windows-1252');
	}

	static $map = null;
	if ($map === null)
	{
		$map = array(
			"\r\n" => "\n", "\r" => "\n", "\u{2028}" => "\n", "\u{2029}" => "\n",
			"\t" => ' ', "\u{00A0}" => ' ', "\u{202F}" => ' ', "\u{205F}" => ' ', "\u{3000}" => ' ',
			"\u{200B}" => '', "\u{200C}" => '', "\u{200D}" => '', "\u{2060}" => '', "\u{FEFF}" => '', "\u{00AD}" => '',
			"\u{2010}" => '-', "\u{2011}" => '-', "\u{2012}" => '-', "\u{2015}" => '-', "\u{2043}" => '-',
			"\u{2032}" => "'", "\u{2035}" => "'", "\u{2033}" => '"',
			"\u{2122}" => '(TM)', "\u{2120}" => '(SM)', "\u{2116}" => 'No.',
			"\u{2264}" => '<=', "\u{2265}" => '>=', "\u{2260}" => '!=', "\u{2248}" => '~',
			"\u{2190}" => '<-', "\u{2192}" => '->', "\u{2194}" => '<->', "\u{21D2}" => '=>',
			"\u{2610}" => '[ ]', "\u{2611}" => '[X]', "\u{2612}" => '[X]', "\u{2713}" => '[X]', "\u{2714}" => '[X]',
			"\u{FB01}" => 'fi', "\u{FB02}" => 'fl',
		);
		//Assorted bullets (incl. Word's private-use Symbol-font bullets) -> the bullet the font has
		foreach (array(0x2023, 0x2219, 0x25AA, 0x25AB, 0x25A0, 0x25A1, 0x25CB, 0x25CF, 0x25E6, 0x25BA, 0x25B8,
			       0x27A2, 0x27A4, 0x2756, 0x29BF, 0xF0A7, 0xF0B7, 0xF0D8, 0xF0FC, 0xF076, 0xF0A8) as $cp)
		{
			$map[mb_chr($cp, 'UTF-8')] = "\u{2022}";
		}
		//Other Unicode spaces
		for ($cp = 0x2000; $cp <= 0x200A; $cp++)
		{
			$map[mb_chr($cp, 'UTF-8')] = ' ';
		}
	}
	$text	= strtr($text, $map);

	//Anything else the font can't draw: transliterate to ASCII if possible, otherwise drop it
	$text	= preg_replace_callback('/[^\x{0A}\x{20}-\x{7E}]/u', function ($m)
	{
		$ch	= $m[0];
		$cp	= mb_ord($ch, 'UTF-8');
		if ($cp !== false && rl_glyph_ok($cp)) return $ch;
		if ($cp < 0x20 || ($cp >= 0x7F && $cp < 0xA0)) return '';	//Control characters
		$ascii	= @iconv('UTF-8', 'ASCII//TRANSLIT//IGNORE', $ch);
		return ($ascii === false || $ascii === '?') ? '' : $ascii;
	}, $text);

	if ($single_line)
	{
		$text	= str_replace("\n", ' ', $text);
	}

	$text	= preg_replace('/ {2,}/', ' ', $text);		//Runs of spaces
	$text	= preg_replace('/ *\n */', "\n", $text);	//Spaces hugging line breaks
	$text	= preg_replace("/\n{3,}/", "\n\n", $text);	//At most one blank line in a row
	return trim($text);
}

function rl_width($text, $font, $size)
{
	if ($text === '') return 0;
	$b	= imagettfbbox($size, 0, $font, $text);
	return $b[2] - $b[0];
}

/*
 * Split one word that's too wide for a line (URLs, part numbers, long emails) into pieces,
 * preferring to break after URL-ish punctuation, falling back to a hard character break.
 */
function rl_break_word($word, $font, $size, $max_width)
{
	$pieces	= array();
	$chars	= preg_split('//u', $word, -1, PREG_SPLIT_NO_EMPTY);
	while (count($chars))
	{
		$line		= '';
		$last_break	= 0;
		$taken		= 0;
		foreach ($chars as $i => $ch)
		{
			if (rl_width($line.$ch, $font, $size) > $max_width && $taken > 0) break;
			$line	.= $ch;
			$taken	= $i + 1;
			if (strpos('/-_.?&=,;:@)', $ch) !== false) $last_break = $taken;
		}
		//Break at punctuation if that keeps at least half the line, otherwise hard-break
		$cut		= ($taken < count($chars) && $last_break > $taken / 2) ? $last_break : $taken;
		$pieces[]	= implode('', array_slice($chars, 0, $cut));
		$chars		= array_slice($chars, $cut);
	}
	return $pieces;
}

/*
 * Word-wrap to a pixel width.  Returns an array of lines; blank lines (paragraph breaks) are kept.
 */
function rl_wrap($text, $font, $size, $max_width)
{
	$lines	= array();
	foreach (explode("\n", $text) as $paragraph)
	{
		if (trim($paragraph) === '')
		{
			$lines[] = '';
			continue;
		}
		$line	= '';
		foreach (explode(' ', $paragraph) as $word)
		{
			if ($word === '') continue;
			$candidate	= ($line === '') ? $word : "$line $word";
			if (rl_width($candidate, $font, $size) <= $max_width)
			{
				$line	= $candidate;
				continue;
			}
			if ($line !== '') $lines[] = $line;
			if (rl_width($word, $font, $size) <= $max_width)
			{
				$line	= $word;
			} else
			{
				$pieces	= rl_break_word($word, $font, $size, $max_width);
				$line	= array_pop($pieces);
				foreach ($pieces as $p) $lines[] = $p;
			}
		}
		$lines[] = $line;
	}
	return $lines;
}

//Shorten a line with an ellipsis until it fits
function rl_ellipsize($text, $font, $size, $max_width)
{
	if (rl_width($text, $font, $size) <= $max_width) return $text;
	while (mb_strlen($text) > 1 && rl_width(rtrim($text)."\u{2026}", $font, $size) > $max_width)
	{
		$text	= mb_substr($text, 0, -1);
	}
	return rtrim($text, " ,;:-")."\u{2026}";
}

/*
 * Single line of text in a fixed-width slot: shrink the font down to $min_size, then ellipsize.
 * Returns array(size, text).
 */
function rl_fit_line($text, $font, $max_width, $size, $min_size)
{
	$text	= rl_clean($text, true);
	while ($size > $min_size && rl_width($text, $font, $size) > $max_width)
	{
		$size--;
	}
	return array($size, rl_ellipsize($text, $font, $size, $max_width));
}

//Font ascent/descent in pixels at a size (from the tallest/deepest common glyphs)
function rl_metrics($font, $size)
{
	$b	= imagettfbbox($size, 0, $font, "ÁHdgjpqy");
	return array(-$b[7], $b[1]);	//ascent above baseline, descent below
}

/*
 * Multi-line text in a box spanning y = $top..$bottom: wrap, shrink until every line fits
 * between the box edges, then ellipsize the last line if it still doesn't.
 * Returns array(size, lines, first_baseline_y, line_height).
 */
function rl_fit_block($text, $font, $max_width, $top, $bottom, $size, $min_size, $clean = true)
{
	if ($clean) $text = rl_clean($text);
	for (; $size >= $min_size; $size--)
	{
		list($asc, $desc)	= rl_metrics($font, $size);
		$lh			= (int)round($size * RL_LINE_SPACING);
		$max_lines		= max(1, (int)floor(($bottom - $top - $asc - $desc) / $lh) + 1);
		$lines			= rl_wrap($text, $font, $size, $max_width);
		if (count($lines) <= $max_lines) return array($size, $lines, $top + $asc, $lh);
	}
	$size	= $min_size;
	$lines	= array_slice($lines, 0, $max_lines);
	$lines[$max_lines - 1] = rl_ellipsize($lines[$max_lines - 1]."\u{2026}", $font, $size, $max_width);
	return array($size, $lines, $top + $asc, $lh);
}

/*
 * Draw lines one at a time with explicit spacing.  $align 'left' uses $x as the left edge,
 * 'center' uses $x as the center of each line.
 */
function rl_draw_lines($im, $lines, $font, $size, $color, $x, $y, $line_height = null, $align = 'left')
{
	if ($line_height === null) $line_height = round($size * RL_LINE_SPACING);
	foreach ((array)$lines as $i => $line)
	{
		if ($line === '') continue;
		$lx	= ($align === 'center') ? $x - rl_width($line, $font, $size) / 2 : $x;
		imagettftext($im, $size, 0, (int)round($lx), (int)round($y + $i * $line_height), $color, $font, $line);
	}
}

//Drop blank lines from the start and end of a page's worth of lines
function rl_trim_lines($lines)
{
	while (count($lines) && trim($lines[0]) === '') array_shift($lines);
	while (count($lines) && trim(end($lines)) === '') array_pop($lines);
	return $lines;
}
?>
