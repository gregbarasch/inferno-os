
/* return a time structure for GMT */
Tm* gmt(ulong tim);

/* convert a time structure from local or gmt into a text string */
char* text(Tm* t);