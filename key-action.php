<?php
include('global.inc');
include('queue-render.inc');

// Deliberately not renderQueueFragment(): this swaps only the key control, the
// way favStar() swaps only the star. A singer at the mic taps this several
// times in a row, and routing it through the queue renderer would spend
// /local/user/me + /local/queue on OpenKJ's Qt main thread for every tap. The
// response carries the resulting key, so one POST is the whole cost.
//
// okj_user() is skipped for the same reason - the API rejects a bad token
// itself, and it is the only thing that can say whether this singer is the one
// actually holding the mic.
$direction = isset($_POST['direction']) ? $_POST['direction'] : '';

// What the button was rendered at, so a failure can put the control back the
// way it was rather than where the tap was aiming.
$current = isset($_POST['current']) ? (int)$_POST['current'] : 0;

if (okj_token() === '')
{
  okj_key_controls($current, 'Please sign in first.');
  exit;
}

$res = okj_post('/local/request/key', array(
  'token' => okj_token(),
  'direction' => $direction,
));

if (!$res['ok'])
{
  // Unreachable and refused look the same in a control this small, so both get
  // the message rather than the offline treatment - there is no room for a
  // banner here, and the queue around it will show the outage anyway.
  okj_key_controls($current, $res['error']);
  exit;
}

// at_limit rides along in the response but needs no special handling: the key
// simply comes back unchanged, and okj_key_controls() disables the button that
// had nowhere left to go.
okj_key_controls(isset($res['data']['key_change']) ? $res['data']['key_change'] : $current);

?>
