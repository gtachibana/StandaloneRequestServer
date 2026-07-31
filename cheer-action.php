<?php
include('global.inc');

// The most-tapped button in the app, so this is deliberately the least this
// server can do: one POST, no token, no okj_user(), no queue re-render, and an
// empty response. The buttons are hx-swap="none" - the count they sit beside is
// updated locally on tap and then corrected by the /local/events "cheer" frame,
// which is the only value that knows about everybody else in the room too.
//
// Contrast key-action.php, which does render its control back: a key nudge has
// exactly one right answer and only the API knows it. A cheer has no answer
// worth waiting for.
$reaction = isset($_POST['reaction']) ? $_POST['reaction'] : '';

$res = okj_post('/local/cheer', array('reaction' => $reaction));

// throttled means the room is hammering the button harder than OpenKJ will take
// - the API flags it precisely so a client can tell it apart from a real refusal
// and drop it on the floor. A lost tap in the middle of an ovation is not worth
// putting a message in front of anyone for.
//
// Status 0 is the API being unreachable, which the rotation around this button
// reports on its next poll.
//
// What is left is a genuine refusal, and for a bar we only render when
// capabilities said cheers were on, that is very nearly always the KJ having
// switched them off since. Forgetting the cache makes the next queue render
// stop offering the buttons instead of waiting out the TTL.
if (!$res['ok'] && $res['status'] !== 0 && empty($res['data']['throttled']))
{
  okj_capabilities_forget();
}
?>
