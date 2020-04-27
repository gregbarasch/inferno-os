#include "dat.h"

char *wkday[7] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
char *weekday[7] = {
		"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
char *month[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
int dmsize[12] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
};
int ldmsize[12] = {
		31, 29, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
};

Tm*
gmt(ulong tim)
{
	Tm *xtime = (struct Tm*) malloc(sizeof(struct Tm));

	// break initial number into days
	unsigned hms = tim % 86400;
	ulong day = tim / 86400;
	if(hms < 0){
		hms += 86400;
		day -=1;
	}

	// generate hours:minutes:seconds
	xtime->sec = hms % 60;
	unsigned dl = hms / 60;
	xtime->min = dl % 60;
	dl /= 60;
	xtime->hour = dl;

	// day is the day number.
	// generate day of the week.
	// The addend is 4 mod 7 (1/1/1970 was Thursday)
	xtime->wday = (day + 7340036) % 7;

	// year number
	if(day >= 0)
		for(dl = 70; day >= dysize(dl+1900); dl++)
			day -= dysize(dl+1900);
	else
		for(dl = 90; day < 0; dl--)
			day += dysize(dl+1900-1);
	xtime->year = dl;
	unsigned d0 = day;
	xtime->yday = d0;

	// generate month
	int *dmsz;
	if(dysize(dl+1900) == 366)
		dmsz = ldmsize;
	else
		dmsz = dmsize;
	for(dl = 0; d0 >= dmsz[dl]; dl++)
		d0 -= dmsz[dl];
	xtime->mday = d0 + 1;
	xtime->mon = dl;
	sprint(xtime->zone, "GMT");
	xtime->tzoff = 0;
	return xtime;
}

char*
text(Tm* t)
{
	char *tstr = (char *) malloc(256);

	int year = 1900+t->year;
	sprint(tstr, "%s %s %.2d %.2d:%.2d:%.2d %s %d",
		   wkday[t->wday],
		   month[t->mon],
		   t->mday,
		   t->hour,
		   t->min,
		   t->sec,
		   t->zone,
		   year);

	return tstr;
}

int
dysize(int y)
{
	if(y%4 == 0 && (y%100 != 0 || y%400 == 0))
		return 366;
	return 365;
}