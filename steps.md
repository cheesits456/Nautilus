# ./src/nautilus-window.c
## line 1392-1396 add
```c
    gtk_places_sidebar_set_show_recent (GTK_PLACES_SIDEBAR (window->places_sidebar),
                                        FALSE);

    gtk_places_sidebar_set_show_starred_location (GTK_PLACES_SIDEBAR (window->places_sidebar),
                                                  FALSE);
```


# ./src/nautilus-enums.h
## line 33-37 change
```c
    NAUTILUS_CANVAS_ICON_SIZE_SMALL    = 30,
    NAUTILUS_CANVAS_ICON_SIZE_STANDARD = 45,
    NAUTILUS_CANVAS_ICON_SIZE_LARGE    = 60,
    NAUTILUS_CANVAS_ICON_SIZE_LARGER   = 90,
    NAUTILUS_CANVAS_ICON_SIZE_LARGEST  = 80,
```
## line 51-54 change
```c
    NAUTILUS_LIST_ICON_SIZE_SMALL    = 30,
    NAUTILUS_LIST_ICON_SIZE_STANDARD = 40,
    NAUTILUS_LIST_ICON_SIZE_LARGE    = 50,
    NAUTILUS_LIST_ICON_SIZE_LARGER   = 60,
```


# ./src/nautilus-file.c
## line 5324-5331 change
```c
    // int zoom_level;

    // zoom_level = size * scale;

    // if (zoom_level <= NAUTILUS_LIST_ICON_SIZE_STANDARD)
    // {
    //     return TRUE;
    // }
```

# ./icons
replace files in this folder with files from previous version


# find and replace all
`Access and organize your files` > `Access and organize your files\nFork by cheesits456`