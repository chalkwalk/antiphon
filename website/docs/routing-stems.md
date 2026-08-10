---
id: routing-stems
title: Recording the jam as stems
sidebar_position: 4
---

# Recording the jam as stems

This is the reason Antiphon is a plugin at all.

Every other NINJAM client hands you a stereo mixdown of the room. Antiphon can
give each remote player their own output bus, so your DAW records them as
separate tracks -- editable, processable, mixable, like any other recording you
made that day.

## Routing players to their own tracks

1. Get into a jam.
2. For each player you want separately: click **Output bus: [+]** to add a
   stereo bus, then set that player's channel to it in its dropdown.
3. In your DAW, route Antiphon's extra outputs to tracks. How depends on the
   host -- in Reaper the plugin's extra output channels appear on the track and
   you route them onward; in Bitwig, use the plugin's multi-out.
4. Record-arm those tracks and hit record.

**Bus 1 is the main mix.** Anything you leave on bus 1 stays there, so you can
pull two people out onto their own tracks and leave the rest summed. You do not
have to route everybody.

:::tip
Add the buses *before* you start recording. Adding one mid-take changes the
plugin's I/O topology, and while Antiphon does tell the host about that, hosts
vary in how gracefully they handle it.
:::

## Sending more than one channel yourself

If you are playing guitar and singing, send them separately so the room can mix
you properly.

1. **Input bus: [+]** to add a second plugin input, and route your second source
   to it in the DAW.
2. **Channel: [+]** to add a second strip.
3. Set the new strip's **Input bus** dropdown to bus 2.
4. **Name them.** "Guitar" and "Vox" beats "Channel 1" and "Channel 2" --
   everyone in the room sees these names, and they are what people will use to
   mix you.
5. Tick **Mono** on anything that is genuinely mono. It halves what you upload,
   which matters on a domestic connection.

Other players now see you as one card with two faders, and can turn your guitar
down without turning your voice down.

## A note on levels

Remote channels default to **0.25**, not unity. This matches the reference
client, and it is deliberate: at unity you would be roughly 12 dB louder than
everyone else expects. If a stem seems quiet on the track, that is why -- and it
is the right place to fix it, on your own fader, rather than by making yourself
louder to the room.

## Recording the session outside the DAW

Antiphon can also archive a session to disk itself, and there is an offline tool
that turns that archive into WAV stems:

```bash
./build/tools/AntiphonStems_artefacts/AntiphonStems <session-dir> -o stems/
```

This is useful when you were not recording in a DAW at the time, or were running
standalone. The stems come out aligned to interval boundaries and annotated with
who played what.
