#!/usr/bin/perl
#
# Generate a composition table, using Unicode data files as input
#
# Input: UnicodeData.txt and CompositionExclusions.txt
# Output: unicode_norm_table.h
#
# Copyright (c) 2000-2017, PostgreSQL Global Development Group

use strict;
use warnings;

my $output_file = "unicode_norm_table.h";

my $FH;

# Read list of codes that should be excluded from re-composition.
my @composition_exclusion_codes = ();
open($FH, '<', "CompositionExclusions.txt")
  or die "Could not open CompositionExclusions.txt: $!.";
while (my $line = <$FH>)
{
	if ($line =~ /^([[:xdigit:]]+)/)
	{
		push @composition_exclusion_codes, $1;
	}
}
close $FH;

# Read entries from UnicodeData.txt into a list, and a hash table. We need
# three fields from each row: the codepoint, canonical combining class,
# and character decomposition mapping
my @characters     = ();
my %character_hash = ();
open($FH, '<', "UnicodeData.txt")
  or die "Could not open UnicodeData.txt: $!.";
while (my $line = <$FH>)
{

	# Split the line wanted and get the fields needed:
	# - Unicode code value
	# - Canonical Combining Class
	# - Character Decomposition Mapping
	my @elts   = split(';', $line);
	my $code   = $elts[0];
	my $class  = $elts[3];
	my $decomp = $elts[5];

	# Skip codepoints above U+10FFFF. They cannot be represented in 4 bytes
	# in UTF-8, and PostgreSQL doesn't support UTF-8 characters longer than
	# 4 bytes. (This is just pro forma, as there aren't any such entries in
	# the data file, currently.)
	next if hex($code) > 0x10FFFF;

	# Skip characters with no decompositions and a class of 0, to reduce the
	# table size.
	next if $class eq '0' && $decomp eq '';

	my %char_entry = (code => $code, class => $class, decomp => $decomp);
	push(@characters, \%char_entry);
	$character_hash{$code} = \%char_entry;
}
close $FH;

my $num_characters = scalar @characters;

# Start writing out the output file
open my $OUTPUT, '>', $output_file
  or die "Could not open output file $output_file: $!\n";

print $OUTPUT <<HEADER;
/*-------------------------------------------------------------------------
 *
 * unicode_norm_table.h
 *	  Composition table used for Unicode normalization
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/common/unicode_norm_table.h
 *
 *-------------------------------------------------------------------------
 */

/*
 * File auto-generated by src/common/unicode/generate-unicode_norm_table.pl,
 * do not edit. There is deliberately not an #ifndef PG_UNICODE_NORM_TABLE_H
 * here.
 */
typedef struct
{
	uint32		codepoint;		/* Unicode codepoint */
	uint8		comb_class;		/* combining class of character */
	uint8		dec_size_flags; /* size and flags of decomposition code list */
	uint16		dec_index;		/* index into UnicodeDecomp_codepoints, or the
								 * decomposition itself if DECOMP_INLINE */
} pg_unicode_decomposition;

#define DECOMP_NO_COMPOSE	0x80	/* don't use for re-composition */
#define DECOMP_INLINE		0x40	/* decomposition is stored inline in dec_index */

#define DECOMPOSITION_SIZE(x) ((x)->dec_size_flags & 0x3F)
#define DECOMPOSITION_NO_COMPOSE(x) (((x)->dec_size_flags & DECOMP_NO_COMPOSE) != 0)
#define DECOMPOSITION_IS_INLINE(x) (((x)->dec_size_flags & DECOMP_INLINE) != 0)

/* Table of Unicode codepoints and their decompositions */
static const pg_unicode_decomposition UnicodeDecompMain[$num_characters] =
{
HEADER

my $decomp_index  = 0;
my $decomp_string = "";

my $last_code = $characters[-1]->{code};
foreach my $char (@characters)
{
	my $code   = $char->{code};
	my $class  = $char->{class};
	my $decomp = $char->{decomp};

	# The character decomposition mapping field in UnicodeData.txt is a list
	# of unicode codepoints, separated by space. But it can be prefixed with
	# so-called compatibility formatting tag, like "<compat>", or "<font>".
	# The entries with compatibility formatting tags should not be used for
	# re-composing characters during normalization, so flag them in the table.
	# (The tag doesn't matter, only whether there is a tag or not)
	my $compat = 0;
	if ($decomp =~ /\<.*\>/)
	{
		$compat = 1;
		$decomp =~ s/\<[^][]*\>//g;
	}
	my @decomp_elts = split(" ", $decomp);

	# Decomposition size
	# Print size of decomposition
	my $decomp_size = scalar(@decomp_elts);

	my $first_decomp = shift @decomp_elts;

	my $flags   = "";
	my $comment = "";

	if ($decomp_size == 2)
	{

		# Should this be used for recomposition?
		if ($compat)
		{
			$flags .= " | DECOMP_NO_COMPOSE";
			$comment = "compatibility mapping";
		}
		elsif ($character_hash{$first_decomp}
			&& $character_hash{$first_decomp}->{class} != 0)
		{
			$flags .= " | DECOMP_NO_COMPOSE";
			$comment = "non-starter decomposition";
		}
		else
		{
			foreach my $lcode (@composition_exclusion_codes)
			{
				if ($lcode eq $char->{code})
				{
					$flags .= " | DECOMP_NO_COMPOSE";
					$comment = "in exclusion list";
					last;
				}
			}
		}
	}

	if ($decomp_size == 0)
	{
		print $OUTPUT "\t{0x$code, $class, 0$flags, 0}";
	}
	elsif ($decomp_size == 1 && length($first_decomp) <= 4)
	{

		# The decomposition consists of a single codepoint, and it fits
		# in a uint16, so we can store it "inline" in the main table.
		$flags .= " | DECOMP_INLINE";
		print $OUTPUT "\t{0x$code, $class, 1$flags, 0x$first_decomp}";
	}
	else
	{
		print $OUTPUT
		  "\t{0x$code, $class, $decomp_size$flags, $decomp_index}";

		# Now save the decompositions into a dedicated area that will
		# be written afterwards.  First build the entry dedicated to
		# a sub-table with the code and decomposition.
		$decomp_string .= ",\n" if ($decomp_string ne "");

		$decomp_string .= "\t /* $decomp_index */ 0x$first_decomp";
		foreach (@decomp_elts)
		{
			$decomp_string .= ", 0x$_";
		}

		$decomp_index = $decomp_index + $decomp_size;
	}

	# Print a comma after all items except the last one.
	print $OUTPUT "," unless ($code eq $last_code);
	if ($comment ne "")
	{

		# If the line is wide already, indent the comment with one tab,
		# otherwise with two. This is to make the output match the way
		# pgindent would mangle it. (This is quite hacky. To do this
		# properly, we should actually track how long the line is so far,
		# but this works for now.)
		print $OUTPUT "\t" if ($decomp_index < 10);

		print $OUTPUT "\t/* $comment */" if ($comment ne "");
	}
	print $OUTPUT "\n";
}
print $OUTPUT "\n};\n\n";

# Print the array of decomposed codes.
print $OUTPUT <<HEADER;
/* codepoints array  */
static const uint32 UnicodeDecomp_codepoints[$decomp_index] =
{
$decomp_string
};
HEADER

close $OUTPUT;
