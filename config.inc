<?php
/*
 * Settings and secrets for the NASA procurement bot.
 * They live in config.ini next to this file (never committed - see config.example.ini).
 * Set NASAPROC_CONFIG=/path/to/other.ini to use a different file.
 */
function cfg($key, $default = null)
{
	static $c = null;
	if ($c === null)
	{
		$path	= getenv('NASAPROC_CONFIG') ?: __DIR__.'/config.ini';
		$c	= is_readable($path) ? parse_ini_file($path, false, INI_SCANNER_RAW) : false;
		if ($c === false)
		{
			fwrite(STDERR, "Missing or unreadable $path - copy config.example.ini to config.ini and fill it in.\n");
			exit(1);
		}
	}
	return (isset($c[$key]) && $c[$key] !== '') ? $c[$key] : $default;
}
?>
