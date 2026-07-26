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

// Base URL of the same embedded API *as the phone sees it*. The queue view
// subscribes to GET /local/events (Server-Sent Events) here to learn about
// rotation changes instead of polling for them, so this has to be a host the
// browser can reach directly - $openkjApiBase above is resolved server-side and
// is often 127.0.0.1 or a Docker-internal name, neither of which works here.
// Leave empty to disable the stream and poll only.
$openkjEventStreamBase = '';

// How often, in seconds, phones re-poll the queue view. The stream carries
// updates when it is configured, so the poll is only a safety net there - which
// is why it is much slower. Older OpenKJ builds have no /local/events and the
// stream shuts itself down on the 404, leaving the fallback poll in charge.
$queuePollSeconds = 10;
$queueStreamPollSeconds = 60;

?>
