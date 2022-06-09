# bar_stats
![demo](https://github.com/zhado/bar_stats/blob/master/common/demo_small.gif?raw=true)
Designed to be used with i3status/swaybar/waybar. Instead of running multiple oneliners which run multiple processes
periodically and clutter up process viewers like htop, with bar_stats its just one process that starts with your window manager and outputs
everything to stdout/fifo. I haven't tested it, but bar_stats probably uses less cpu time than grep piped into awk piped into sed... etc.

This code is meant to just work with my setup. All the file paths are hardcoded. However they can be easily changed.

In gif above it outputs: pulseaudio output volume level, cpu usage, cpu temperature, cpu core frequencies and cpu frequency governor, ram usage, disk usage,
network activity, battery percentage and discharge/charge rate in watts, display brightness level, date and time.

# Building
just run:
```bash
gcc main.c -lpulse -lpthread -lm -O3 -o ./bar_stats
```
# Usage
after that you can just cat the fifo which bar_stats writes to:
```bash
cat /tmp/bar_stats_fifo
```
or add it to your status bar config. Example for waybar:
```json
    "custom/stats": {
      "exec": "cat /tmp/bar_stats_fifo",
      "format": "{}",
      "tooltip": false
    },      
```
