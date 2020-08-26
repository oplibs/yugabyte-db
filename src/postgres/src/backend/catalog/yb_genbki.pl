#!/usr/bin/perl -w
#----------------------------------------------------------------------
#
# yb_genbki.pl
#    Perl script that generates yb_postgres.bki, postgres.description,
#    postgres.shdescription, and symbol definition headers from specially
#    formatted header files and data files.  The BKI files are used to
#    initialize the postgres template database.
#
#    YugaByte: This is copied and modified from Postgres's standard genbki.pl.
#    The main changes are that indexes are now generated as clauses of the
#    table creation statement rather than standalone statements (so we can
#    choose a primary key from them). Additionally toast relation declarations
#    are removed and pg_depend/pg_shdepend tables now have oids.
#
# Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
# Portions Copyright (c) 1994, Regents of the University of California
#
# src/backend/catalog/genbki.pl
#
#----------------------------------------------------------------------

use Catalog;

use strict;
use warnings;

my @input_files;
my $output_path = '';
my $major_version;

# Process command line switches.
while (@ARGV)
{
	my $arg = shift @ARGV;
	if ($arg !~ /^-/)
	{
		push @input_files, $arg;
	}
	elsif ($arg =~ /^-o/)
	{
		$output_path = length($arg) > 2 ? substr($arg, 2) : shift @ARGV;
	}
	elsif ($arg =~ /^--set-version=(.*)$/)
	{
		$major_version = $1;
		die "Invalid version string.\n"
		  if !($major_version =~ /^\d+$/);
	}
	else
	{
		usage();
	}
}

# Sanity check arguments.
die "No input files.\n" if !@input_files;
die "--set-version must be specified.\n" if !defined $major_version;

# Make sure output_path ends in a slash.
if ($output_path ne '' && substr($output_path, -1) ne '/')
{
	$output_path .= '/';
}

# Read all the files into internal data structures.
my @catnames;
my %catalogs;
my %catalog_data;
my @toast_decls;
my @index_decls;
my %oidcounts;

foreach my $header (@input_files)
{
	$header =~ /(.+)\.h$/
	  or die "Input files need to be header files.\n";
	my $datfile = "$1.dat";

	my $catalog = Catalog::ParseHeader($header);
	my $catname = $catalog->{catname};
	my $schema  = $catalog->{columns};

	if (defined $catname)
	{
		push @catnames, $catname;
		$catalogs{$catname} = $catalog;
	}

	# While checking for duplicated OIDs, we ignore the pg_class OID and
	# rowtype OID of bootstrap catalogs, as those are expected to appear
	# in the initial data for pg_class and pg_type.  For regular catalogs,
	# include these OIDs.  (See also Catalog::FindAllOidsFromHeaders
	# if you change this logic.)
	if (!$catalog->{bootstrap})
	{
		$oidcounts{ $catalog->{relation_oid} }++
		  if ($catalog->{relation_oid});
		$oidcounts{ $catalog->{rowtype_oid} }++
		  if ($catalog->{rowtype_oid});
	}

	# Not all catalogs have a data file.
	if (-e $datfile)
	{
		my $data = Catalog::ParseData($datfile, $schema, 0);
		$catalog_data{$catname} = $data;

		# Check for duplicated OIDs while we're at it.
		foreach my $row (@$data)
		{
			$oidcounts{ $row->{oid} }++ if defined $row->{oid};
		}
	}

	# If the header file contained toast or index info, build BKI
	# commands for those, which we'll output later.
	foreach my $toast (@{ $catalog->{toasting} })
	{
		push @toast_decls,
		  sprintf "declare toast %s %s on %s\n",
		  $toast->{toast_oid}, $toast->{toast_index_oid},
		  $toast->{parent_table};
		$oidcounts{ $toast->{toast_oid} }++;
		$oidcounts{ $toast->{toast_index_oid} }++;
	}
	foreach my $index (@{ $catalog->{indexing} })
	{
		push @index_decls,
		  sprintf "declare %sindex %s %s %s\n",
		  $index->{is_unique} ? 'unique ' : '',
		  $index->{index_name}, $index->{index_oid},
		  $index->{index_decl};
		$oidcounts{ $index->{index_oid} }++;
	}
}

# Complain and exit if we found any duplicate OIDs.
# While duplicate OIDs would only cause a failure if they appear in
# the same catalog, our project policy is that manually assigned OIDs
# should be globally unique, to avoid confusion.
my $found = 0;
foreach my $oid (keys %oidcounts)
{
	next unless $oidcounts{$oid} > 1;
	print STDERR "Duplicate OIDs detected:\n" if !$found;
	print STDERR "$oid\n";
	$found++;
}
die "found $found duplicate OID(s) in catalog data\n" if $found;

# Fetch some special data that we will substitute into the output file.
# CAUTION: be wary about what symbols you substitute into the .bki file here!
# It's okay to substitute things that are expected to be really constant
# within a given Postgres release, such as fixed OIDs.  Do not substitute
# anything that could depend on platform or configuration.  (The right place
# to handle those sorts of things is in initdb.c's bootstrap_template1().)
my $BOOTSTRAP_SUPERUSERID =
  Catalog::FindDefinedSymbolFromData($catalog_data{pg_authid},
	'BOOTSTRAP_SUPERUSERID');
my $PG_CATALOG_NAMESPACE =
  Catalog::FindDefinedSymbolFromData($catalog_data{pg_namespace},
	'PG_CATALOG_NAMESPACE');


# Build lookup tables for OID macro substitutions and for pg_attribute
# copies of pg_type values.

# index access method OID lookup
my %amoids;
foreach my $row (@{ $catalog_data{pg_am} })
{
	$amoids{ $row->{amname} } = $row->{oid};
}

# opclass OID lookup
my %opcoids;
foreach my $row (@{ $catalog_data{pg_opclass} })
{
	# There is no unique name, so we need to combine access method
	# and opclass name.
	my $key = sprintf "%s/%s", $row->{opcmethod}, $row->{opcname};
	$opcoids{$key} = $row->{oid};
}

# operator OID lookup
my %operoids;
foreach my $row (@{ $catalog_data{pg_operator} })
{
	# There is no unique name, so we need to invent one that contains
	# the relevant type names.
	my $key = sprintf "%s(%s,%s)",
	  $row->{oprname}, $row->{oprleft}, $row->{oprright};
	$operoids{$key} = $row->{oid};
}

# opfamily OID lookup
my %opfoids;
foreach my $row (@{ $catalog_data{pg_opfamily} })
{
	# There is no unique name, so we need to combine access method
	# and opfamily name.
	my $key = sprintf "%s/%s", $row->{opfmethod}, $row->{opfname};
	$opfoids{$key} = $row->{oid};
}

# procedure OID lookup
my %procoids;
foreach my $row (@{ $catalog_data{pg_proc} })
{
	# Generate an entry under just the proname (corresponds to regproc lookup)
	my $prokey = $row->{proname};
	if (defined $procoids{$prokey})
	{
		$procoids{$prokey} = 'MULTIPLE';
	}
	else
	{
		$procoids{$prokey} = $row->{oid};
	}

	# Also generate an entry using proname(proargtypes).  This is not quite
	# identical to regprocedure lookup because we don't worry much about
	# special SQL names for types etc; we just use the names in the source
	# proargtypes field.  These *should* be unique, but do a multiplicity
	# check anyway.
	$prokey .= '(' . join(',', split(/\s+/, $row->{proargtypes})) . ')';
	if (defined $procoids{$prokey})
	{
		$procoids{$prokey} = 'MULTIPLE';
	}
	else
	{
		$procoids{$prokey} = $row->{oid};
	}
}

# type lookups
my %typeoids;
my %types;
foreach my $row (@{ $catalog_data{pg_type} })
{
	$typeoids{ $row->{typname} } = $row->{oid};
	$types{ $row->{typname} }    = $row;
}

# Map catalog name to OID lookup.
my %lookup_kind = (
	pg_am       => \%amoids,
	pg_opclass  => \%opcoids,
	pg_operator => \%operoids,
	pg_opfamily => \%opfoids,
	pg_proc     => \%procoids,
	pg_type     => \%typeoids);


# Open temp files
my $tmpext  = ".tmp$$";
my $bkifile = $output_path . 'yb_postgres.bki';
open my $bki, '>', $bkifile . $tmpext
  or die "can't open $bkifile$tmpext: $!";

# Generate postgres.bki, postgres.description, postgres.shdescription,
# and pg_*_d.h headers.

# version marker for .bki file
print $bki "# PostgreSQL $major_version\n";

print $bki "yb_check_if_initdb_is_already_done\n";

# vars to hold data needed for schemapg.h
my %schemapg_entries;
my @tables_needing_macros;


# YB-specific pre-processing of the catalogs.

# Primary keys used for system tables.
my %pkidxs;

# produce output, one catalog at a time
foreach my $catname (@catnames)
{
	my $catalog = $catalogs{$catname};

	# .bki CREATE command for this catalog
	print $bki "create $catname $catalog->{relation_oid}"
	  . $catalog->{shared_relation}
	  . $catalog->{bootstrap}
	  . $catalog->{without_oids}
	  . $catalog->{rowtype_oid_clause};

	my $first = 1;

	print $bki "\n (\n";
	my $schema = $catalog->{columns};
	my %attnames;
	my $attnum = 0;
	foreach my $column (@$schema)
	{
		$attnum++;
		my $attname = $column->{name};
		my $atttype = $column->{type};

		# Build hash of column names for use later
		$attnames{$attname} = 1;

		# Emit column definitions
		if (!$first)
		{
			print $bki " ,\n";
		}
		$first = 0;

		print $bki " $attname = $atttype";

		if (defined $column->{forcenotnull})
		{
			print $bki " FORCE NOT NULL";
		}
		elsif (defined $column->{forcenull})
		{
			print $bki " FORCE NULL";
		}
	}
	print $bki "\n )\n";

    # Select a unique index on the table if available as the table's primary key
    # and add it after the table definition:
    # - prefer the oid one if available,
    # - for pg_attribute, pg_attribute_relid_attnum_index fits better as the primary key,
    # - otherwise default to first index.
    my $pkidxname;
    my $pkidx;
	foreach (@index_decls)
	{
        my ($unique, $idxname, $oid, $icatname, $columns) =
            /declare (unique )?index (.*) (\d+) on (.+) using (.+)/;
        die "Unrecognized index declaration $_" if !$idxname;
        if ($icatname eq $catname && $unique)
        {
            if (($columns eq "btree(oid oid_ops)") ||
                ($icatname eq "pg_attribute" && $idxname eq "pg_attribute_relid_attnum_index"))
            {
                ($pkidxname, $pkidx) = ($idxname, $_);
                last;
            }
            ($pkidxname, $pkidx) = ($idxname, $_) if (!$pkidx);
        }
	}
    if ($pkidx)
    {
		$pkidx =~ s/unique index/primary index/;
        print $bki " yb_" . $pkidx;
        $pkidxs{$pkidxname} = 1;
	}

	# Open it, unless it's a bootstrap catalog (create bootstrap does this
	# automatically)
	if (!$catalog->{bootstrap})
	{
		print $bki "open $catname\n";
	}

	# For pg_attribute.h, we generate data entries ourselves.
	if ($catname eq 'pg_attribute')
	{
		gen_pg_attribute($schema);
	}

	# Ordinary catalog with a data file
	foreach my $row (@{ $catalog_data{$catname} })
	{
		my %bki_values = %$row;

		# Complain about unrecognized keys; they are presumably misspelled
		foreach my $key (keys %bki_values)
		{
			next
			  if $key eq "oid"
			  || $key eq "oid_symbol"
			  || $key eq "array_type_oid"
			  || $key eq "descr"
			  || $key eq "autogenerated"
			  || $key eq "line_number";
			die sprintf "unrecognized field name \"%s\" in %s.dat line %s\n",
			  $key, $catname, $bki_values{line_number}
			  if (!exists($attnames{$key}));
		}

		# Perform required substitutions on fields
		foreach my $column (@$schema)
		{
			my $attname = $column->{name};
			my $atttype = $column->{type};

			# Substitute constant values we acquired above.
			# (It's intentional that this can apply to parts of a field).
			$bki_values{$attname} =~ s/\bPGUID\b/$BOOTSTRAP_SUPERUSERID/g;
			$bki_values{$attname} =~ s/\bPGNSP\b/$PG_CATALOG_NAMESPACE/g;

			# Replace OID synonyms with OIDs per the appropriate lookup rule.
			#
			# If the column type is oidvector or _oid, we have to replace
			# each element of the array as per the lookup rule.
			if ($column->{lookup})
			{
				my $lookup = $lookup_kind{ $column->{lookup} };
				my @lookupnames;
				my @lookupoids;

				die "unrecognized BKI_LOOKUP type " . $column->{lookup}
				  if !defined($lookup);

				if ($atttype eq 'oidvector')
				{
					@lookupnames = split /\s+/, $bki_values{$attname};
					@lookupoids = lookup_oids($lookup, $catname, \%bki_values,
						@lookupnames);
					$bki_values{$attname} = join(' ', @lookupoids);
				}
				elsif ($atttype eq '_oid')
				{
					if ($bki_values{$attname} ne '_null_')
					{
						$bki_values{$attname} =~ s/[{}]//g;
						@lookupnames = split /,/, $bki_values{$attname};
						@lookupoids =
						  lookup_oids($lookup, $catname, \%bki_values,
							@lookupnames);
						$bki_values{$attname} = sprintf "{%s}",
						  join(',', @lookupoids);
					}
				}
				else
				{
					$lookupnames[0] = $bki_values{$attname};
					@lookupoids = lookup_oids($lookup, $catname, \%bki_values,
						@lookupnames);
					$bki_values{$attname} = $lookupoids[0];
				}
			}
		}

		# Special hack to generate OID symbols for pg_type entries
		# that lack one.
		if ($catname eq 'pg_type' and !exists $bki_values{oid_symbol})
		{
			my $symbol = form_pg_type_symbol($bki_values{typname});
			$bki_values{oid_symbol} = $symbol
			  if defined $symbol;
		}

		# Write to yb_postgres.bki
		print_bki_insert(\%bki_values, $schema);
	}

	print $bki "close $catname\n";
}

# Declare secondary indexes on the tables.
foreach (@index_decls)
{
	my ($unique, $idxname, $oid, $icatname, $columns) =
		/declare (unique )?index (.*) (\d+) on (.+) using (.+)/;
	s/unique index/primary index/ if $idxname && $pkidxs{$idxname};
	print $bki $_;
}

# last command in the BKI file: build the indexes declared above
print $bki "build indices\n";

# Any information needed for the BKI that is not contained in a pg_*.h header
# (i.e., not contained in a header with a CATALOG() statement) comes here

# We're done emitting data
close $bki;

# Finally, rename the completed files into place.
Catalog::RenameTempFile($bkifile,     $tmpext);

exit 0;

#################### Subroutines ########################


# For each catalog marked as needing a schema macro, generate the
# per-user-attribute data to be incorporated into schemapg.h.  Also, for
# bootstrap catalogs, emit pg_attribute entries into the .bki file
# for both user and system attributes.
sub gen_pg_attribute
{
	my $schema = shift;

	my @attnames;
	foreach my $column (@$schema)
	{
		push @attnames, $column->{name};
	}

	foreach my $table_name (@catnames)
	{
		my $table = $catalogs{$table_name};

		# Currently, all bootstrap catalogs also need schemapg.h
		# entries, so skip if it isn't to be in schemapg.h.
		next if !$table->{schema_macro};

		$schemapg_entries{$table_name} = [];
		push @tables_needing_macros, $table_name;

		# Generate entries for user attributes.
		my $attnum       = 0;
		my $priornotnull = 1;
		foreach my $attr (@{ $table->{columns} })
		{
			$attnum++;
			my %row;
			$row{attnum}   = $attnum;
			$row{attrelid} = $table->{relation_oid};

			morph_row_for_pgattr(\%row, $schema, $attr, $priornotnull);
			$priornotnull &= ($row{attnotnull} eq 't');

			# If it's bootstrapped, put an entry in postgres.bki.
			print_bki_insert(\%row, $schema) if $table->{bootstrap};
		}

		# Generate entries for system attributes.
		# We only need postgres.bki entries, not schemapg.h entries.
		if ($table->{bootstrap})
		{
			$attnum = 0;
			my @SYS_ATTRS = (
				{ name => 'ctid',     type => 'tid' },
				{ name => 'oid',      type => 'oid' },
				{ name => 'xmin',     type => 'xid' },
				{ name => 'cmin',     type => 'cid' },
				{ name => 'xmax',     type => 'xid' },
				{ name => 'cmax',     type => 'cid' },
				{ name => 'tableoid', type => 'oid' });
			foreach my $attr (@SYS_ATTRS)
			{
				$attnum--;
				my %row;
				$row{attnum}        = $attnum;
				$row{attrelid}      = $table->{relation_oid};
				$row{attstattarget} = '0';

				# Omit the oid column if the catalog doesn't have them
				next
				  if $table->{without_oids}
				  && $attr->{name} eq 'oid';

				morph_row_for_pgattr(\%row, $schema, $attr, 1);
				print_bki_insert(\%row, $schema);
			}
		}
	}
	return;
}

# Given $pgattr_schema (the pg_attribute schema for a catalog sufficient for
# AddDefaultValues), $attr (the description of a catalog row), and
# $priornotnull (whether all prior attributes in this catalog are not null),
# modify the $row hashref for print_bki_insert.  This includes setting data
# from the corresponding pg_type element and filling in any default values.
# Any value not handled here must be supplied by caller.
sub morph_row_for_pgattr
{
	my ($row, $pgattr_schema, $attr, $priornotnull) = @_;
	my $attname = $attr->{name};
	my $atttype = $attr->{type};

	$row->{attname} = $attname;

	# Copy the type data from pg_type, and add some type-dependent items
	my $type = $types{$atttype};

	$row->{atttypid}   = $type->{oid};
	$row->{attlen}     = $type->{typlen};
	$row->{attbyval}   = $type->{typbyval};
	$row->{attstorage} = $type->{typstorage};
	$row->{attalign}   = $type->{typalign};

	# set attndims if it's an array type
	$row->{attndims} = $type->{typcategory} eq 'A' ? '1' : '0';
	$row->{attcollation} = $type->{typcollation};

	if (defined $attr->{forcenotnull})
	{
		$row->{attnotnull} = 't';
	}
	elsif (defined $attr->{forcenull})
	{
		$row->{attnotnull} = 'f';
	}
	elsif ($priornotnull)
	{

		# attnotnull will automatically be set if the type is
		# fixed-width and prior columns are all NOT NULL ---
		# compare DefineAttr in bootstrap.c. oidvector and
		# int2vector are also treated as not-nullable.
		$row->{attnotnull} =
		    $type->{typname} eq 'oidvector'  ? 't'
		  : $type->{typname} eq 'int2vector' ? 't'
		  : $type->{typlen} eq 'NAMEDATALEN' ? 't'
		  : $type->{typlen} > 0              ? 't'
		  :                                    'f';
	}
	else
	{
		$row->{attnotnull} = 'f';
	}

	Catalog::AddDefaultValues($row, $pgattr_schema, 'pg_attribute');
	return;
}

# Write an entry to postgres.bki.
sub print_bki_insert
{
	my $row    = shift;
	my $schema = shift;

	my @bki_values;
	my $oid = $row->{oid} ? "OID = $row->{oid} " : '';

	foreach my $column (@$schema)
	{
		my $attname   = $column->{name};
		my $atttype   = $column->{type};
		my $bki_value = $row->{$attname};

		# Fold backslash-zero to empty string if it's the entire string,
		# since that represents a NUL char in C code.
		$bki_value = '' if $bki_value eq '\0';

		# Handle single quotes by doubling them, and double quotes by
		# converting them to octal escapes, because that's what the
		# bootstrap scanner requires.  We do not process backslashes
		# specially; this allows escape-string-style backslash escapes
		# to be used in catalog data.
		$bki_value =~ s/'/''/g;
		$bki_value =~ s/"/\\042/g;

		# Quote value if needed.  We need not quote values that satisfy
		# the "id" pattern in bootscanner.l, currently "[-A-Za-z0-9_]+".
		$bki_value = sprintf(qq'"%s"', $bki_value)
		  if length($bki_value) == 0
		  or $bki_value =~ /[^-A-Za-z0-9_]/;

		push @bki_values, $bki_value;
	}
	printf $bki "insert %s( %s )\n", $oid, join(' ', @bki_values);
	return;
}

# Perform OID lookups on an array of OID names.
# If we don't have a unique value to substitute, warn and
# leave the entry unchanged.
# (A warning seems sufficient because the bootstrap backend will reject
# non-numeric values anyway.  So we might as well detect multiple problems
# within this genbki.pl run.)
sub lookup_oids
{
	my ($lookup, $catname, $bki_values, @lookupnames) = @_;

	my @lookupoids;
	foreach my $lookupname (@lookupnames)
	{
		my $lookupoid = $lookup->{$lookupname};
		if (defined($lookupoid) and $lookupoid ne 'MULTIPLE')
		{
			push @lookupoids, $lookupoid;
		}
		else
		{
			push @lookupoids, $lookupname;
			warn sprintf
			  "unresolved OID reference \"%s\" in %s.dat line %s\n",
			  $lookupname, $catname, $bki_values->{line_number}
			  if $lookupname ne '-' and $lookupname ne '0';
		}
	}
	return @lookupoids;
}

# Determine canonical pg_type OID #define symbol from the type name.
sub form_pg_type_symbol
{
	my $typename = shift;

	# Skip for rowtypes of bootstrap catalogs, since they have their
	# own naming convention defined elsewhere.
	return
	     if $typename eq 'pg_type'
	  or $typename eq 'pg_proc'
	  or $typename eq 'pg_attribute'
	  or $typename eq 'pg_class';

	# Transform like so:
	#  foo_bar  ->  FOO_BAROID
	# _foo_bar  ->  FOO_BARARRAYOID
	$typename =~ /(_)?(.+)/;
	my $arraystr = $1 ? 'ARRAY' : '';
	my $name = uc $2;
	return $name . $arraystr . 'OID';
}

sub usage
{
	die <<EOM;
Usage: yb_genbki.pl [options] header...

Options:
    -o               output path
    --set-version    PostgreSQL version number for initdb cross-check

yb_genbki.pl generates BKI files and symbol definition
headers from specially formatted header files and .dat
files.  The BKI files are used to initialize the
postgres template database.

It should be used in YugaByte mode instead of the standard genbki.pl, as it
includes special handling of indexes, toast relations and
pg_depend/pg_shdepend tables.

EOM
}
