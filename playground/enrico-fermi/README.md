# Enrico Fermi in vector ink

[portrait.fcd](portrait.fcd) is an artistic FidoCad drawing of Enrico Fermi.
Facial and clothing contours are editable polylines; diagonal strokes and
cross-hatching model the light and shadow. The drawing contains no embedded
bitmap and needs no symbol libraries.

```sh
fcai gui playground/enrico-fermi/portrait.fcd
fcai render playground/enrico-fermi/portrait.fcd --scale 3 --margin 28 -o docs/images/example_enrico_fermi.png
```

The reference is [Enrico Fermi, 1943–1949](https://commons.wikimedia.org/wiki/File:Enrico_Fermi_1943-49.jpg),
Department of Energy–Office of Public Affairs, National Archives identifier
558578, restored by Yann. The reference photograph is in the public domain as
a work of the United States federal government.

This interpretation isolates the bust, traces selected contours, and replaces
photographic tones with vector hatching. The `.fcd` file, rather than the PNG
preview, is the editable artwork. It passes FCD round-trip validation.
