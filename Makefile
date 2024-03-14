MODULE_big = orafce
OBJS= parse_keyword.o convert.o file.o datefce.o magic.o others.o plvstr.o plvdate.o shmmc.o plvsubst.o utility.o plvlex.o alert.o pipe.o sqlparse.o putline.o assert.o plunit.o random.o aggregate.o orafce.o varchar2.o nvarchar2.o charpad.o charlen.o replace_empty_string.o

EXTENSION = orafce

DATA = orafce--3.14.sql orafce--3.2--3.3.sql orafce--3.3--3.4.sql orafce--3.4--3.5.sql orafce--3.5--3.6.sql orafce--3.6--3.7.sql orafce--3.7--3.8.sql orafce--3.8--3.9.sql orafce--3.9--3.10.sql orafce--3.10--3.11.sql orafce--3.11--3.12.sql orafce--3.12--3.13.sql orafce--3.13--3.14.sql
DOCS = README.asciidoc COPYRIGHT.orafce INSTALL.orafce

PG_CONFIG ?= pg_config

# make "all" the default target
all:

REGRESS = orafce orafce2 dbms_output dbms_utility files varchar2 nvarchar2 aggregates nlssort dbms_random

#REGRESS_OPTS = --load-language=plpgsql --schedule=parallel_schedule --encoding=utf8
REGRESS_OPTS = --schedule=parallel_schedule --encoding=utf8

SHLIB_LINK += -L$(YB_BUILD_ROOT)/lib -lyb_pggate

#override CFLAGS += -Wextra -Wimplicit-fallthrough=0

ifdef NO_PGXS
subdir = contrib/$(MODULE_big)
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
else
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
endif

ifeq ($(enable_nls), yes)
SHLIB_LINK += $(filter -lintl,$(LIBS))
endif

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

plvlex.o: sqlparse.o

sqlparse.o: $(srcdir)/sqlscan.c

$(srcdir)/sqlparse.h: $(srcdir)/sqlparse.c ;

$(srcdir)/sqlparse.c: sqlparse.y
ifdef BISON
	$(BISON) -d $(BISONFLAGS) -o $@ $<
else
ifdef YACC
	$(YACC) -d $(YFLAGS) -p cube_yy $<
	mv -f y.tab.c sqlparse.c
	mv -f y.tab.h sqlparse.h
else
	bison -d $(BISONFLAGS) -o $@ $<
endif
endif

$(srcdir)/sqlscan.c: sqlscan.l
ifdef FLEX
	$(FLEX) $(FLEXFLAGS) -o'$@' $<
else
	flex $(FLEXFLAGS) -o'$@' $<
endif

distprep: $(srcdir)/sqlparse.c $(srcdir)/sqlscan.c

maintainer-clean:
	rm -f $(srcdir)/sqlparse.c $(srcdir)/sqlscan.c $(srcdir)/sqlparse.h $(srcdir)/y.tab.c $(srcdir)/y.tab.h
