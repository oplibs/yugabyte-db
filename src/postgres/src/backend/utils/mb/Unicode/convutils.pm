#
# Copyright (c) 2001-2022, PostgreSQL Global Development Group
#
# src/backend/utils/mb/Unicode/convutils.pm

package convutils;

use strict;
use warnings;

use Carp;
use Exporter 'import';

our @EXPORT =
  qw( NONE TO_UNICODE FROM_UNICODE BOTH read_source print_conversion_tables);

# Constants used in the 'direction' field of the character maps
use constant {
	NONE         => 0,
	TO_UNICODE   => 1,
	FROM_UNICODE => 2,
	BOTH         => 3
};

#######################################################################
# read_source - common routine to read source file
#
# fname ; input file name
#
sub read_source
{
	my ($fname) = @_;
	my @r;

	open(my $in, '<', $fname) || die("cannot open $fname");

	while (<$in>)
	{
		next if (/^#/);
		chop;

		next if (/^$/);    # Ignore empty lines

		next if (/^0x([0-9A-F]+)\s+(#.*)$/);

		# The Unicode source files have three columns
		# 1: The "foreign" code (in hex)
		# 2: Unicode code point (in hex)
		# 3: Unicode name
		if (!/^0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(#.*)$/)
		{
			print STDERR "READ ERROR at line $. in $fname: $_\n";
			exit;
		}
		my $out = {
			code      => hex($1),
			ucs       => hex($2),
			comment   => $4,
			direction => BOTH,
			f         => $fname,
			l         => $.
		};

		# Ignore pure ASCII mappings. PostgreSQL character conversion code
		# never even passes these to the conversion code.
		next if ($out->{code} < 0x80 || $out->{ucs} < 0x80);

		push(@r, $out);
	}
	close($in);

	return \@r;
}

##################################################################
# print_conversion_tables - output mapping tables
#
# print_conversion_tables($this_script, $csname, \%charset)
#
# this_script - the name of the *caller script* of this feature
# csname      - character set name other than ucs
# charset     - ref to character set array
#
# Input character set array format:
#
# Each element in the character set array is a hash. Each hash has the following fields:
#   direction  - BOTH, TO_UNICODE, or FROM_UNICODE (or NONE, to ignore the entry altogether)
#   ucs        - Unicode code point
#   ucs_second - Second Unicode code point, if this is a "combined" character.
#   code       - Byte sequence in the "other" character set, as an integer
#   comment    - Text representation of the character
#   f          - Source filename
#   l          - Line number in source file
#
sub print_conversion_tables
{
	my ($this_script, $csname, $charset) = @_;

	print_conversion_tables_direction($this_script, $csname, FROM_UNICODE,
		$charset);
	print_conversion_tables_direction($this_script, $csname, TO_UNICODE,
		$charset);
	return;
}

#############################################################################
# INTERNAL ROUTINES

#######################################################################
# print_conversion_tables_direction - write the whole content of C source of radix tree
#
# print_conversion_tables_direction($this_script, $csname, $direction, \%charset, $tblwidth)
#
# this_script - the name of the *caller script* of this feature
# csname      - character set name other than ucs
# direction   - desired direction, TO_UNICODE or FROM_UNICODE
# charset     - ref to character set array
#
sub print_conversion_tables_direction
{
	my ($this_script, $csname, $direction, $charset) = @_;

	my $fname;
	my $tblname;
	if ($direction == TO_UNICODE)
	{
		$fname   = lc("${csname}_to_utf8.map");
		$tblname = lc("${csname}_to_unicode_tree");

		print "- Writing ${csname}=>UTF8 conversion table: $fname\n";
	}
	else
	{
		$fname   = lc("utf8_to_${csname}.map");
		$tblname = lc("${csname}_from_unicode_tree");

		print "- Writing UTF8=>${csname} conversion table: $fname\n";
	}

	open(my $out, '>', $fname) || die("cannot open $fname");

	print $out "/* src/backend/utils/mb/Unicode/$fname */\n";
	print $out "/* This file is generated by $this_script */\n\n";

	# Collect regular, non-combined, mappings, and create the radix tree from them.
	my $charmap = &make_charmap($out, $charset, $direction, 0);
	print_radix_table($out, $tblname, $charmap);

	# Collect combined characters, and create combined character table (if any)
	my $charmap_combined = &make_charmap_combined($charset, $direction);

	if (scalar @{$charmap_combined} > 0)
	{
		if ($direction == TO_UNICODE)
		{
			print_to_utf8_combined_map($out, $csname, $charmap_combined, 1);
		}
		else
		{
			print_from_utf8_combined_map($out, $csname, $charmap_combined, 1);
		}
	}

	close($out);
	return;
}

sub print_from_utf8_combined_map
{
	my ($out, $charset, $table, $verbose) = @_;

	my $last_comment = "";

	printf $out "\n/* Combined character map */\n";
	printf $out
	  "static const pg_utf_to_local_combined ULmap${charset}_combined[%d] = {",
	  scalar(@$table);
	my $first = 1;
	foreach my $i (sort { $a->{utf8} <=> $b->{utf8} } @$table)
	{
		print($out ",") if (!$first);
		$first = 0;
		print $out "\t/* $last_comment */"
		  if ($verbose && $last_comment ne "");

		printf $out "\n  {0x%08x, 0x%08x, 0x%04x}",
		  $i->{utf8}, $i->{utf8_second}, $i->{code};
		if ($verbose >= 2)
		{
			$last_comment =
			  sprintf("%s:%d %s", $i->{f}, $i->{l}, $i->{comment});
		}
		elsif ($verbose >= 1)
		{
			$last_comment = $i->{comment};
		}
	}
	print $out "\t/* $last_comment */" if ($verbose && $last_comment ne "");
	print $out "\n};\n";
	return;
}

sub print_to_utf8_combined_map
{
	my ($out, $charset, $table, $verbose) = @_;

	my $last_comment = "";

	printf $out "\n/* Combined character map */\n";
	printf $out
	  "static const pg_local_to_utf_combined LUmap${charset}_combined[%d] = {",
	  scalar(@$table);

	my $first = 1;
	foreach my $i (sort { $a->{code} <=> $b->{code} } @$table)
	{
		print($out ",") if (!$first);
		$first = 0;
		print $out "\t/* $last_comment */"
		  if ($verbose && $last_comment ne "");

		printf $out "\n  {0x%04x, 0x%08x, 0x%08x}",
		  $i->{code}, $i->{utf8}, $i->{utf8_second};

		if ($verbose >= 2)
		{
			$last_comment =
			  sprintf("%s:%d %s", $i->{f}, $i->{l}, $i->{comment});
		}
		elsif ($verbose >= 1)
		{
			$last_comment = $i->{comment};
		}
	}
	print $out "\t/* $last_comment */" if ($verbose && $last_comment ne "");
	print $out "\n};\n";
	return;
}

#######################################################################
# print_radix_table(<output handle>, <table name>, <charmap hash ref>)
#
# Input: A hash, mapping an input character to an output character.
#
# Constructs a radix tree from the hash, and prints it out as a C-struct.
#
sub print_radix_table
{
	my ($out, $tblname, $c) = @_;

	###
	### Build radix trees in memory, for 1-, 2-, 3- and 4-byte inputs. Each
	### radix tree is represented as a nested hash, each hash indexed by
	### input byte
	###
	my %b1map;
	my %b2map;
	my %b3map;
	my %b4map;
	foreach my $in (keys %$c)
	{
		my $out = $c->{$in};

		if ($in <= 0xff)
		{
			$b1map{$in} = $out;
		}
		elsif ($in <= 0xffff)
		{
			my $b1 = $in >> 8;
			my $b2 = $in & 0xff;

			$b2map{$b1}{$b2} = $out;
		}
		elsif ($in <= 0xffffff)
		{
			my $b1 = $in >> 16;
			my $b2 = ($in >> 8) & 0xff;
			my $b3 = $in & 0xff;

			$b3map{$b1}{$b2}{$b3} = $out;
		}
		elsif ($in <= 0xffffffff)
		{
			my $b1 = $in >> 24;
			my $b2 = ($in >> 16) & 0xff;
			my $b3 = ($in >> 8) & 0xff;
			my $b4 = $in & 0xff;

			$b4map{$b1}{$b2}{$b3}{$b4} = $out;
		}
		else
		{
			die sprintf("up to 4 byte code is supported: %x", $in);
		}
	}

	my @segments;

	###
	### Build a linear list of "segments", from the nested hashes.
	###
	### Each segment is a lookup table, keyed by the next byte in the input.
	### The segments are written out physically to one big array in the final
	### step, but logically, they form a radix tree. Or rather, four radix
	### trees: one for 1-byte inputs, another for 2-byte inputs, 3-byte
	### inputs, and 4-byte inputs.
	###
	### Each segment is represented by a hash with following fields:
	###
	### comment => <string to output as a comment>
	### label => <label that can be used to refer to this segment from elsewhere>
	### values => <a hash, keyed by byte, 0-0xff>
	###
	### Entries in 'values' can be integers (for leaf-level segments), or
	### string labels, pointing to a segment with that label. Any missing
	### values are treated as zeros. If 'values' hash is missing altogether,
	### it's treated as all-zeros.
	###
	### Subsequent steps will enrich the segments with more fields.
	###

	# Add the segments for the radix trees themselves.
	push @segments,
	  build_segments_from_tree("Single byte table", "1-byte", 1, \%b1map);
	push @segments,
	  build_segments_from_tree("Two byte table", "2-byte", 2, \%b2map);
	push @segments,
	  build_segments_from_tree("Three byte table", "3-byte", 3, \%b3map);
	push @segments,
	  build_segments_from_tree("Four byte table", "4-byte", 4, \%b4map);

	###
	### Find min and max index used in each level of each tree.
	###
	### These are stored separately, and we can then leave out the unused
	### parts of every segment. (When using the resulting tree, you must
	### check each input byte against the min and max.)
	###
	my %min_idx;
	my %max_idx;
	foreach my $seg (@segments)
	{
		my $this_min = $min_idx{ $seg->{depth} }->{ $seg->{level} };
		my $this_max = $max_idx{ $seg->{depth} }->{ $seg->{level} };

		foreach my $i (keys %{ $seg->{values} })
		{
			$this_min = $i if (!defined $this_min || $i < $this_min);
			$this_max = $i if (!defined $this_max || $i > $this_max);
		}

		$min_idx{ $seg->{depth} }{ $seg->{level} } = $this_min;
		$max_idx{ $seg->{depth} }{ $seg->{level} } = $this_max;
	}

	# Copy the mins and max's back to every segment, for convenience.
	foreach my $seg (@segments)
	{
		$seg->{min_idx} = $min_idx{ $seg->{depth} }{ $seg->{level} };
		$seg->{max_idx} = $max_idx{ $seg->{depth} }{ $seg->{level} };
	}

	###
	### Prepend a dummy all-zeros map to the beginning.
	###
	### A 0 is an invalid value anywhere in the table, and this allows us to
	### point to 0 offset from any table, to get a 0 result.
	###

	# Find the max range between min and max indexes in any of the segments.
	my $widest_range = 0;
	foreach my $seg (@segments)
	{
		my $this_range = $seg->{max_idx} - $seg->{min_idx};
		$widest_range = $this_range if ($this_range > $widest_range);
	}

	unshift @segments,
	  {
		header  => "Dummy map, for invalid values",
		min_idx => 0,
		max_idx => $widest_range,
		label   => "dummy map"
	  };

	###
	### Eliminate overlapping zeros
	###
	### For each segment, if there are zero values at the end of, and there
	### are also zero values at the beginning of the next segment, we can
	### overlay the tail of this segment with the head of next segment, to
	### save space.
	###
	### To achieve that, we subtract the 'max_idx' of each segment with the
	### amount of zeros that can be overlaid.
	###
	for (my $j = 0; $j < $#segments - 1; $j++)
	{
		my $seg     = $segments[$j];
		my $nextseg = $segments[ $j + 1 ];

		# Count the number of zero values at the end of this segment.
		my $this_trail_zeros = 0;
		for (
			my $i = $seg->{max_idx};
			$i >= $seg->{min_idx} && !$seg->{values}->{$i};
			$i--)
		{
			$this_trail_zeros++;
		}

		# Count the number of zeros at the beginning of next segment.
		my $next_lead_zeros = 0;
		for (
			my $i = $nextseg->{min_idx};
			$i <= $nextseg->{max_idx} && !$nextseg->{values}->{$i};
			$i++)
		{
			$next_lead_zeros++;
		}

		# How many zeros in common?
		my $overlaid_trail_zeros =
		  ($this_trail_zeros > $next_lead_zeros)
		  ? $next_lead_zeros
		  : $this_trail_zeros;

		$seg->{overlaid_trail_zeros} = $overlaid_trail_zeros;
		$seg->{max_idx} = $seg->{max_idx} - $overlaid_trail_zeros;
	}

	###
	### Replace label references with real offsets.
	###
	### So far, the non-leaf segments have referred to other segments by
	### their labels. Replace them with numerical offsets from the beginning
	### of the final array. You cannot move, add, or remove segments after
	### this step, as that would invalidate the offsets calculated here!
	###
	my $flatoff = 0;
	my %segmap;

	# First pass: assign offsets to each segment, and build hash
	# of label => offset.
	foreach my $seg (@segments)
	{
		$seg->{offset} = $flatoff;
		$segmap{ $seg->{label} } = $flatoff;
		$flatoff += $seg->{max_idx} - $seg->{min_idx} + 1;
	}
	my $tblsize = $flatoff;

	# Second pass: look up the offset of each label reference in the hash.
	foreach my $seg (@segments)
	{
		while (my ($i, $val) = each %{ $seg->{values} })
		{
			if (!($val =~ /^[0-9,.E]+$/))
			{
				my $segoff = $segmap{$val};
				if ($segoff)
				{
					$seg->{values}->{$i} = $segoff;
				}
				else
				{
					die "no segment with label $val";
				}
			}
		}
	}

	# Also look up the positions of the roots in the table.
	# Missing map represents dummy mapping.
	my $b1root = $segmap{"1-byte"} || 0;
	my $b2root = $segmap{"2-byte"} || 0;
	my $b3root = $segmap{"3-byte"} || 0;
	my $b4root = $segmap{"4-byte"} || 0;

	# And the lower-upper values of each level in each radix tree.
	# Missing values represent zero.
	my $b1_lower = $min_idx{1}{1} || 0;
	my $b1_upper = $max_idx{1}{1} || 0;

	my $b2_1_lower = $min_idx{2}{1} || 0;
	my $b2_1_upper = $max_idx{2}{1} || 0;
	my $b2_2_lower = $min_idx{2}{2} || 0;
	my $b2_2_upper = $max_idx{2}{2} || 0;

	my $b3_1_lower = $min_idx{3}{1} || 0;
	my $b3_1_upper = $max_idx{3}{1} || 0;
	my $b3_2_lower = $min_idx{3}{2} || 0;
	my $b3_2_upper = $max_idx{3}{2} || 0;
	my $b3_3_lower = $min_idx{3}{3} || 0;
	my $b3_3_upper = $max_idx{3}{3} || 0;

	my $b4_1_lower = $min_idx{4}{1} || 0;
	my $b4_1_upper = $max_idx{4}{1} || 0;
	my $b4_2_lower = $min_idx{4}{2} || 0;
	my $b4_2_upper = $max_idx{4}{2} || 0;
	my $b4_3_lower = $min_idx{4}{3} || 0;
	my $b4_3_upper = $max_idx{4}{3} || 0;
	my $b4_4_lower = $min_idx{4}{4} || 0;
	my $b4_4_upper = $max_idx{4}{4} || 0;

	###
	### Find the maximum value in the whole table, to determine if we can
	### use uint16 or if we need to use uint32.
	###
	my $max_val = 0;
	foreach my $seg (@segments)
	{
		foreach my $val (values %{ $seg->{values} })
		{
			$max_val = $val if ($val > $max_val);
		}
	}

	my $datatype = ($max_val <= 0xffff) ? "uint16" : "uint32";

	# For formatting, determine how many values we can fit on a single
	# line, and how wide each value needs to be to align nicely.
	my $vals_per_line;
	my $colwidth;

	if ($max_val <= 0xffff)
	{
		$vals_per_line = 8;
		$colwidth      = 4;
	}
	elsif ($max_val <= 0xffffff)
	{
		$vals_per_line = 4;
		$colwidth      = 6;
	}
	else
	{
		$vals_per_line = 4;
		$colwidth      = 8;
	}

	###
	### Print the struct and array.
	###
	printf $out "static const $datatype ${tblname}_table[$tblsize];\n";
	printf $out "\n";
	printf $out "static const pg_mb_radix_tree $tblname =\n";
	printf $out "{\n";
	if ($datatype eq "uint16")
	{
		print $out "  ${tblname}_table,\n";
		print $out "  NULL, /* 32-bit table not used */\n";
	}
	if ($datatype eq "uint32")
	{
		print $out "  NULL, /* 16-bit table not used */\n";
		print $out "  ${tblname}_table,\n";
	}
	printf $out "\n";
	printf $out "  0x%04x, /* offset of table for 1-byte inputs */\n",
	  $b1root;
	printf $out "  0x%02x, /* b1_lower */\n", $b1_lower;
	printf $out "  0x%02x, /* b1_upper */\n", $b1_upper;
	printf $out "\n";
	printf $out "  0x%04x, /* offset of table for 2-byte inputs */\n",
	  $b2root;
	printf $out "  0x%02x, /* b2_1_lower */\n", $b2_1_lower;
	printf $out "  0x%02x, /* b2_1_upper */\n", $b2_1_upper;
	printf $out "  0x%02x, /* b2_2_lower */\n", $b2_2_lower;
	printf $out "  0x%02x, /* b2_2_upper */\n", $b2_2_upper;
	printf $out "\n";
	printf $out "  0x%04x, /* offset of table for 3-byte inputs */\n",
	  $b3root;
	printf $out "  0x%02x, /* b3_1_lower */\n", $b3_1_lower;
	printf $out "  0x%02x, /* b3_1_upper */\n", $b3_1_upper;
	printf $out "  0x%02x, /* b3_2_lower */\n", $b3_2_lower;
	printf $out "  0x%02x, /* b3_2_upper */\n", $b3_2_upper;
	printf $out "  0x%02x, /* b3_3_lower */\n", $b3_3_lower;
	printf $out "  0x%02x, /* b3_3_upper */\n", $b3_3_upper;
	printf $out "\n";
	printf $out "  0x%04x, /* offset of table for 4-byte inputs */\n",
	  $b4root;
	printf $out "  0x%02x, /* b4_1_lower */\n", $b4_1_lower;
	printf $out "  0x%02x, /* b4_1_upper */\n", $b4_1_upper;
	printf $out "  0x%02x, /* b4_2_lower */\n", $b4_2_lower;
	printf $out "  0x%02x, /* b4_2_upper */\n", $b4_2_upper;
	printf $out "  0x%02x, /* b4_3_lower */\n", $b4_3_lower;
	printf $out "  0x%02x, /* b4_3_upper */\n", $b4_3_upper;
	printf $out "  0x%02x, /* b4_4_lower */\n", $b4_4_lower;
	printf $out "  0x%02x  /* b4_4_upper */\n", $b4_4_upper;
	print $out "};\n";
	print $out "\n";
	print $out "static const $datatype ${tblname}_table[$tblsize] =\n";
	print $out "{";
	my $off = 0;

	foreach my $seg (@segments)
	{
		printf $out "\n";
		printf $out "  /*** %s - offset 0x%05x ***/\n", $seg->{header}, $off;
		printf $out "\n";

		for (my $i = $seg->{min_idx}; $i <= $seg->{max_idx};)
		{

			# Print the next line's worth of values.
			# XXX pad to begin at a nice boundary
			printf $out "  /* %02x */ ", $i;
			for (my $j = 0;
				$j < $vals_per_line && $i <= $seg->{max_idx}; $j++)
			{
				# missing values represent zero.
				my $val = $seg->{values}->{$i} || 0;

				printf $out " 0x%0*x", $colwidth, $val;
				$off++;
				if ($off != $tblsize)
				{
					print $out ",";
				}
				$i++;
			}
			print $out "\n";
		}
		if ($seg->{overlaid_trail_zeros})
		{
			printf $out
			  "    /* $seg->{overlaid_trail_zeros} trailing zero values shared with next segment */\n";
		}
	}

	# Sanity check.
	if ($off != $tblsize) { die "table size didn't match!"; }

	print $out "};\n";
	return;
}

###
sub build_segments_from_tree
{
	my ($header, $rootlabel, $depth, $map) = @_;

	my @segments;

	if (%{$map})
	{
		@segments =
		  build_segments_recurse($header, $rootlabel, "", 1, $depth, $map);

		# Sort the segments into "breadth-first" order. Not strictly required,
		# but makes the maps nicer to read.
		@segments =
		  sort { $a->{level} cmp $b->{level} or $a->{path} cmp $b->{path} }
		  @segments;
	}

	return @segments;
}

###
sub build_segments_recurse
{
	my ($header, $label, $path, $level, $depth, $map) = @_;

	my @segments;

	if ($level == $depth)
	{
		push @segments,
		  {
			header => $header . ", leaf: ${path}xx",
			label  => $label,
			level  => $level,
			depth  => $depth,
			path   => $path,
			values => $map
		  };
	}
	else
	{
		my %children;

		while (my ($i, $val) = each %$map)
		{
			my $childpath = $path . sprintf("%02x", $i);
			my $childlabel = "$depth-level-$level-$childpath";

			push @segments,
			  build_segments_recurse($header, $childlabel, $childpath,
				$level + 1, $depth, $val);
			$children{$i} = $childlabel;
		}

		push @segments,
		  {
			header => $header . ", byte #$level: ${path}xx",
			label  => $label,
			level  => $level,
			depth  => $depth,
			path   => $path,
			values => \%children
		  };
	}
	return @segments;
}

#######################################################################
# make_charmap - convert charset table to charmap hash
#
# make_charmap(\@charset, $direction)
# charset     - ref to charset table : see print_conversion_tables
# direction   - conversion direction
#
sub make_charmap
{
	my ($out, $charset, $direction, $verbose) = @_;

	croak "unacceptable direction : $direction"
	  if ($direction != TO_UNICODE && $direction != FROM_UNICODE);

	# In verbose mode, print a large comment with the source and comment of
	# each character
	if ($verbose)
	{
		print $out "/*\n";
		print $out "<src>  <dst>    <file>:<lineno> <comment>\n";
	}

	my %charmap;
	foreach my $c (@$charset)
	{

		# combined characters are handled elsewhere
		next if (defined $c->{ucs_second});

		next if ($c->{direction} != $direction && $c->{direction} != BOTH);

		my ($src, $dst) =
		  $direction == TO_UNICODE
		  ? ($c->{code}, ucs2utf($c->{ucs}))
		  : (ucs2utf($c->{ucs}), $c->{code});

		# check for duplicate source codes
		if (defined $charmap{$src})
		{
			printf STDERR
			  "Error: duplicate source code on %s:%d: 0x%04x => 0x%04x, 0x%04x\n",
			  $c->{f}, $c->{l}, $src, $charmap{$src}, $dst;
			exit;
		}
		$charmap{$src} = $dst;

		if ($verbose)
		{
			printf $out "0x%04x 0x%04x %s:%d %s\n", $src, $dst, $c->{f},
			  $c->{l}, $c->{comment};
		}
	}
	if ($verbose)
	{
		print $out "*/\n\n";
	}

	return \%charmap;
}

#######################################################################
# make_charmap_combined - convert charset table to charmap hash
#     with checking duplicate source code
#
# make_charmap_combined(\@charset, $direction)
# charset     - ref to charset table : see print_conversion_tables
# direction   - conversion direction
#
sub make_charmap_combined
{
	my ($charset, $direction) = @_;

	croak "unacceptable direction : $direction"
	  if ($direction != TO_UNICODE && $direction != FROM_UNICODE);

	my @combined;
	foreach my $c (@$charset)
	{
		next if ($c->{direction} != $direction && $c->{direction} != BOTH);

		if (defined $c->{ucs_second})
		{
			my $entry = {
				utf8        => ucs2utf($c->{ucs}),
				utf8_second => ucs2utf($c->{ucs_second}),
				code        => $c->{code},
				comment     => $c->{comment},
				f           => $c->{f},
				l           => $c->{l}
			};
			push @combined, $entry;
		}
	}

	return \@combined;
}

#######################################################################
# convert UCS-4 to UTF-8
#
sub ucs2utf
{
	my ($ucs) = @_;
	my $utf;

	if ($ucs <= 0x007f)
	{
		$utf = $ucs;
	}
	elsif ($ucs > 0x007f && $ucs <= 0x07ff)
	{
		$utf = (($ucs & 0x003f) | 0x80) | ((($ucs >> 6) | 0xc0) << 8);
	}
	elsif ($ucs > 0x07ff && $ucs <= 0xffff)
	{
		$utf =
		  ((($ucs >> 12) | 0xe0) << 16) |
		  (((($ucs & 0x0fc0) >> 6) | 0x80) << 8) | (($ucs & 0x003f) | 0x80);
	}
	else
	{
		$utf =
		  ((($ucs >> 18) | 0xf0) << 24) |
		  (((($ucs & 0x3ffff) >> 12) | 0x80) << 16) |
		  (((($ucs & 0x0fc0) >> 6) | 0x80) << 8) | (($ucs & 0x003f) | 0x80);
	}
	return $utf;
}

1;
