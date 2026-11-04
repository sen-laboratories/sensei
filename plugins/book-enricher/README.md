# BERT, the SEN Book Enrichment Tool

Grabs the metadata of a book from OpenLibrary, and the people who wrote it.

**The book.** The attributes (title, authors, ISBN, pages, publisher, subjects...) and the cover as the thumbnail (`Media:Thumbnail`) are written to the file, which is named after the title if it had no
title. If OpenLibrary finds **more than one book** (up to 20), the user chooses in a window (`sen::ChooseCandidate`, sento): the best first, rated by the rank of OpenLibrary (35%), how close the title is to
the one asked for (30%), how complete the entry is (20%) and how well known the book is (15%, editions and readers). `bert --auto <file>` takes the best without asking (for scripts); cancelling the window
changes nothing.

**The authors** are made all in one place (`Authors.cpp`), from the keys that OpenLibrary gives for the book, so nothing has to be found again afterwards. For each author:

* the author is looked up (`authors/<key>.json`): name, birthday, biography, photo;
* a **person** (`entity/x-vnd.sen-labs.entity.person`) is made as a file next to the book, named after the author: `foaf:name`, `schema:birthDate`, `dc:description` (the biography) and the key
  (`OPENLIB:author_key`);
* the photo is the thumbnail of the person (`Media:Thumbnail`), as the cover is for the book;
* the person is related to the book: an **Authorship** (the SEN server writes the relation and its opposite direction).

A second enrichment of the book finds the people again by their file name and their key, updates them, and does not make a second relation. A person that cannot be looked up is skipped and does not undo
the book or the other authors; without the SEN server the people are made, and the relations come with the next enrichment.


## Settings

BERT has no window of its own: the **Settings window of SEN** (Deskbar menu, *Settings…*, under *Plugins*) shows its settings and gives the plugin back what you change. They are kept in `~/config/settings/sen/bert.settings` (lines of `key = value`, `#` starts a comment; comments and other lines stay when the window changes a value), and the options of the command line win over them for one run. What is replaced is not a setting: the action decides (*Enrich*, or *Enrich (Overwrite)* with the Shift key).

| Key | Default | |
|---|---|---|
| `authors` | yes | make a person of each author of the book |

`choose` (ask which one if several are found) and the most entities that are made (the first authors of a long list) are not set here: they are the same for all plugins and are set once in the Settings window of SEN, under **Plugins** (*Ask which one if several are found*, *Max. number of entities to extract*). An older `choose` or `max_authors` line in the file is ignored; `--auto` takes the best for one run.

