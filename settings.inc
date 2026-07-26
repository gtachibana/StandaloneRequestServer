<?php

// Shown in the page header, unless OpenKJ's own event settings supply a name.
$venueName = 'Sing73k';

// Base URL of the OpenKJ Local Mode embedded API. OpenKJ serves this when
// Settings > app mode is Local Mode and the embedded API is enabled; it
// defaults to port 5050 bound to 0.0.0.0.
$openkjApiBase = 'http://127.0.0.1:5050';

// Seconds to wait on the API before giving up. The embedded API runs on
// OpenKJ's Qt main thread, so keep this short - a slow request there is a
// stalled UI for the KJ.
$openkjTimeout = 4;

// Max songs to pull back per search (the API itself clamps to 500) and per
// browse-by-letter page (clamped to 1000).
$searchLimit = 200;
$browseLimit = 300;

// How often, in seconds, phones re-poll the queue view.
$queuePollSeconds = 10;

?>
