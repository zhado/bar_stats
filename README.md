# bar_stats

Designed to be used with i3status/swaybar/waybar. Instead of running multiple oneliners which run multiple processes
periodically and clutter up process viewers like htop, with bar_stats its just one process that starts with your window manager and outputs
everything to stdout. I haven't tested it, but bar_stats probably uses less cpu time than grep piped into awk piped into sed... etc.

This code is meant to just work with my setup. All the file paths are hardcoded. However they can be easily changed.

# building
just run:
```console
gcc main.c -lpulse -lpthread -lm -O3 -o ./bar_stats
```
after that you can just cat the fifo which bar_stats writes to:
```console
cat /tmp/bar_stats_fifo
```
or add it to your status bar config. Example for waybar:
```console
    "custom/stats": {
      "exec": "cat /tmp/bar_stats_fifo",
      "format": "{}",
      "tooltip": false
    },      
```

