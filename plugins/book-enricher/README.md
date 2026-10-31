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
