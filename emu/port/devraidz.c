#include	"dat.h"
#include	"fns.h"
#include	"error.h"

#define BLKSZ 1024

#define EOFFSET(o) o >= BLKSZ ? (o/BLKSZ/2)*BLKSZ + o%BLKSZ : o
#define OOFFSET(o) o >= BLKSZ ? ((o/BLKSZ/2) +1) * BLKSZ : 0

static int oddbegin(vlong);
static int oddend(long, vlong);
static int begln(long, vlong);
static int endln(long, vlong);
static vlong chanln(Chan*);

enum {
	Qdir,
	Qdata,
	Qctl,
};

Dirtab raidztab[] =
{
    ".",         {Qdir, 0, QTDIR}, 0, 0555,
    "raidzdata", {Qdata}, 0,	0666,
    "raidzctl",  {Qctl},  0,	0666,
};

enum
{
	CMbind,
	CMunbind
};

static
Cmdtab progcmd[] = {
	CMbind,   "bind",   3,
	CMunbind, "unbind", 1,
};

/* used for interleaving blocks */
static Chan *ceven, *codd;

static Chan *
raidzattach(char *spec)
{
	return devattach('z', spec);
}

static Walkqid *
raidzwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, raidztab, nelem(raidztab), devgen);
}

static int
raidzstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, raidztab, nelem(raidztab), devgen);
}

static Chan *
raidzopen(Chan *c, int omode)
{
	return devopen(c, omode, raidztab, nelem(raidztab), devgen);
}

static void
raidzclose(Chan *c)
{
	USED(c);
}

static long
raidzread(Chan *c, void *va, long count, vlong offset)
{
	int i, n, tc, em, om;
	int beg, end;
	long mid;
	int eswtch, ceswtch;
	vlong eoffset, ooffset;
	long ecount, ocount;
	char *bbuf, *vac;
	char evenbuf[count], oddbuf[count];

	if(c->qid.type & QTDIR)
		return devdirread(c, va, count, raidztab, nelem(raidztab), devgen);
	switch(c->qid.path) {
		case Qdata:
			if(ceven == nil || codd == nil)
				return 0;
			if(offset+count > raidztab[1].length)
				count = raidztab[1].length-offset;

			eoffset=EOFFSET(offset);
			ooffset=OOFFSET(offset);
			beg = begln(count, offset);
			end = endln(count, offset);
			mid = count-end-beg;

			ocount = 0;
			ecount = 0;
			if(oddbegin(offset)){
				ocount += beg;
				eswtch = 0;
				ceswtch = 0;
				bbuf = oddbuf;
			}else {
				ecount += beg;
				eswtch = 1;
				ceswtch = 1;
				bbuf = evenbuf;
			}
			if(oddend(count, offset))
				ocount += end;
			else
				ecount += end;

			/* count */
			i = 0;
			while(i<mid){
				if(ceswtch)
					ecount++;
				else
					ocount++;
				i++;
				if(i%BLKSZ == 0)
					ceswtch = ceswtch == 1 ? 0 : 1;
			}

			/* read */
			n = 0;
			n += devtab[ceven->type]->read(ceven, (void*)evenbuf, ecount, eoffset);
			n += devtab[codd->type]->read(codd, (void*)oddbuf, ocount, ooffset);
			if(n != count) // FIXME is it okay to handle this error condition like this?
				return 0;

			/* INTERLEAVE */
			vac = (char*) va;

			/* beginning */
			tc = 0;
			for(i=0; i<beg; i++){
				vac[tc] = bbuf[i];
				tc++;
			}
			/* middle + end */
			i=0;
			em=0;
			om=0;
			while(i<mid+end){
				if(eswtch == 1) {
					vac[tc] = evenbuf[em];
					em++;
				}else {
					vac[tc] = oddbuf[om];
					om++;
				}
				i++;
				tc++;
				if(i%BLKSZ == 0)
					eswtch = eswtch == 1 ? 0 : 1;
			}

			break;
		case Qctl:
			n = readstr(offset, va, count, "There is nothing to read from the ctl file\n");
			break;
		default:
			n = 0;
	}

	return n;
}

static long
raidzwrite(Chan *c, void *va, long count, vlong offset)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	Dir *deven, *dodd;
	int n, nwrite, eind, oind, eoffset, ooffset;
	char evenbuf[count];
	char oddbuf[count];

	switch (c->qid.path){
		case Qctl:
			cb = parsecmd(va, count);
			if(waserror()){
				free(cb);
				nexterror();
			}
			ct = lookupcmd(cb, progcmd, nelem(progcmd));
			switch(ct->index){
			case CMbind:
				if(ceven == nil && codd == nil){
					ceven = namec(cb->f[1], Aopen, ORDWR, 0);
					codd = namec(cb->f[2], Aopen, ORDWR, 0);
					raidztab[1].length = chanln(ceven) + chanln(codd);
					print("Successfully bound %s %s with a total size of %d\n", c2name(ceven), c2name(codd), raidztab[1].length);
				}else
					print("The following 2 channels are already bound: \n\t%s\n\t%s\n", c2name(ceven), c2name(codd));
				break;
			case CMunbind:
				if(ceven != nil){
					cclose(ceven);
					ceven = nil;
				}
				if(codd != nil){
					cclose(codd);
					codd = nil;
				}
				raidztab[1].length = 0;
				break;
			}
			poperror();
			free(cb);
			break;
		case Qdata:
			if(ceven == nil || codd == nil)
				return 0;

			nwrite = 0;
			eind = 0;
			oind = 0;
			while(nwrite < count){
				if(((nwrite+offset) / BLKSZ) % 2 == 0)
					evenbuf[eind++] = ((char*)va)[nwrite];
				else
					oddbuf[oind++] = ((char*)va)[nwrite];
				nwrite++;
			}

			eoffset=EOFFSET(offset);
			ooffset=OOFFSET(offset);

			n = 0;
			if(eind > 0)
				n += devtab[ceven->type]->write(ceven, (void*)evenbuf, eind, eoffset);
			if(oind > 0)
				n += devtab[codd->type]->write(codd, (void*)oddbuf, oind, ooffset);

			raidztab[1].length = chanln(ceven) + chanln(codd);
			break;
	}

	return n;
}

static int
oddend(long count, vlong offset)
{
	int base, rmndr, blks;

	base = (count+offset) / BLKSZ;
	rmndr = (count+offset) % BLKSZ;
	blks = rmndr > 0 ? base+1 : base;
	return blks % 2 == 0;
}

static int
oddbegin(vlong offset)
{
	int blk;
	if(offset<=0)
		return 0;
	blk = (offset / BLKSZ)+1;
	return blk % 2 == 0;
}

static int
begln(long count, vlong offset)
{
	int sz;
	if(offset<=0 || offset%BLKSZ == 0)
		return 0;
	sz = BLKSZ-(offset%BLKSZ);
	return count<sz ? count : sz;
}

static int
endln(long count, vlong offset)
{
	if(offset<=0 || count/BLKSZ < 1)
		return 0;
	return (count+offset) % BLKSZ;
}

static vlong
chanln(Chan* c)
{
	vlong ln;
	Dir *dc;
	dc = chandirstat(c);
	ln = dc != nil ? dc->length : 0;
	free(dc);
	return ln;
}


Dev raidzdevtab = {
		'z',
		"raidz",

		devinit,
		raidzattach,
		raidzwalk,
		raidzstat,
		raidzopen,
		devcreate,
		raidzclose,
		raidzread,
		devbread,
		raidzwrite,
		devbwrite,
		devremove,
		devwstat,
};