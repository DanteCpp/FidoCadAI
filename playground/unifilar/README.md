# Single-line electrical distribution

[distribution.fcd](distribution.fcd) is a unifilar drawing made with `fcai` and
its live GUI. It uses the bundled `elettrotecnica` symbols for an energy meter,
main circuit breaker, RCCB, and three outgoing MCB feeders.

One feeder line represents phase and neutral. The protective-earth bus is
drawn separately in green. Lighting, sockets, and workshop branches demonstrate
the notation; ratings are illustrative, not an installation specification.

```sh
fcai gui playground/unifilar/distribution.fcd
fcai render playground/unifilar/distribution.fcd --scale 4 --margin 20 -o docs/images/example_unifilar.png
```

The drawing passes FCD round-trip validation and has no wire/junction lint
findings. A single-line diagram describes distribution connectivity, rather
than laying out every individual conductor; see Schneider Electric's
[single-line diagram guide](https://productinfo.se.com/edesign_userguide/edesign_user_guide/EN/DOCA0281EN_eDesign_User_Guide%20%28Except%20France%29_0001012752.xml/%24/eDesign_EN_SINGLELINEDIAGRAM_DD00843036).
