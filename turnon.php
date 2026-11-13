<?php
//Generic off script for deltayeet commander
include 'common.inc';

$sqlstring 	= "UPDATE control_data SET control_data = '1' WHERE control_name = 'active'";
$result		= mysqli_query($conn, $sqlstring);
echo "Script has been turned on.\n";

?>