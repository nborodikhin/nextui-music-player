#!/bin/sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir="$script_dir/../src"
fixture_dir="$script_dir/color_call_fixtures"

scan_file() {
    input=$1
    filtered=$(mktemp)
    trap 'rm -f "$filtered"' EXIT HUP INT TERM

    # Remove a marked region through its first textual semicolon.
    awk '
        /^[[:space:]]*\/\/noinspection HardcodedColor[[:space:]]*$/ {
            print ""
            marked = 1
            next
        }
        marked {
            end = index($0, ";")
            if (end == 0) {
                print ""
                next
            }
            printf "%*s%s\n", end, "", substr($0, end + 1)
            marked = 0
            next
        }
        { print }
    ' "$input" > "$filtered"
    awk_status=$?
    if [ "$awk_status" -ne 0 ]; then
        echo "$input:1: source scan failed" >&2
        rm -f "$filtered"
        trap - EXIT HUP INT TERM
        return "$awk_status"
    fi

    perl -0777 -e '
        use strict;
        use warnings;

        my ($display, $source_path, $filtered_path) = @ARGV;
        open my $source_file, "<", $source_path or die "$source_path: $!\n";
        open my $filtered_file, "<", $filtered_path or die "$filtered_path: $!\n";
        local $/;
        my $source = <$source_file>;
        my $text = <$filtered_file>;

        my $number = qr/(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*/;

        sub collect_findings {
            my ($content) = @_;
            my @found;

            my $add_matches = sub {
                my ($label, $pattern) = @_;
                while ($content =~ /$pattern/gms) {
                    my $line = 1 + (substr($content, 0, $-[0]) =~ tr/\n//);
                    push @found, [$-[0], $line, $label];
                }
            };

            $add_matches->("fixed COLOR name",
                qr/\bCOLOR_(?:WHITE|GRAY|BLACK|LIGHT_TEXT|DARK_TEXT|BUTTON_TEXT)\b/);
            $add_matches->("fixed RGB name",
                qr/\bRGB_(?:WHITE|BLACK|GRAY|LIGHT_GRAY|DARK_GRAY)\b/);
            $add_matches->("fixed TRIAD name",
                qr/\bTRIAD_(?:WHITE|BLACK|GRAY|LIGHT_GRAY|DARK_GRAY|LIGHT_TEXT|DARK_TEXT)\b/);
            $add_matches->("numeric CFG_getColor argument",
                qr/\bCFG_getColor\s*\(\s*$number\s*\)/);
            # A packed value given straight to a fill is a fixed color too. A
            # transparent clear of a work surface is not one, thus 0 passes.
            $add_matches->("numeric fill color",
                qr/\bSDL_FillRect\s*\([^;]*,\s*0[xX][0-9a-fA-F]{2,8}[uUlL]*\s*\)/);
            $add_matches->("numeric fill color",
                qr/\bGFX_blitPillColor\s*\([^;]*,\s*0[xX][0-9a-fA-F]{2,8}[uUlL]*\s*,/);

            while ($content =~ /\bSDL_MapRGB(A)?\s*\([^;]*\)/gms) {
                my $match_start = $-[0];
                my $rgba = $1;
                my $call = $&;
                next if defined $rgba && $call =~ /\bSDL_MapRGBA\s*\([^;]*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*\)\s*$/s;
                if (defined $rgba) {
                    next unless $call =~ /,\s*$number\b\s*,/s;
                } else {
                    next unless $call =~ /,\s*$number\b\s*(?:,|\))/s;
                }
                my $line = 1 + (substr($content, 0, $match_start) =~ tr/\n//);
                push @found, [$match_start, $line, "numeric SDL map channels"];
            }

            while ($content =~ /\bSDL_Color\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\])?\s*=\s*\{[^;]*;/gms) {
                my $match_start = $-[0];
                my $statement = $&;
                next unless $statement =~ /=\s*\{\s*\{*\s*(?:$number\b|\.[rgba]\s*=\s*$number\b)/s;
                my $line = 1 + (substr($content, 0, $match_start) =~ tr/\n//);
                push @found, [$match_start, $line, "numeric SDL_Color declaration"];
            }
            while ($content =~ /\(\s*SDL_Color\s*\)\s*\{[^;}]*\}/gms) {
                my $match_start = $-[0];
                my $literal = $&;
                next unless $literal =~ /\{\s*(?:$number\b|\.[rgba]\s*=\s*$number\b)/s;
                my $line = 1 + (substr($content, 0, $match_start) =~ tr/\n//);
                push @found, [$match_start, $line, "numeric SDL_Color compound literal"];
            }

            return @found;
        }

        my @findings = collect_findings($text);
        my @source_findings = collect_findings($source);

        while ($source =~ /^[[:blank:]]*\/\/noinspection HardcodedColor[[:blank:]]*$/gm) {
            my $mark_start = $-[0];
            my $mark_line = 1 + (substr($source, 0, $mark_start) =~ tr/\n//);
            my $region_start = index($source, "\n", $mark_start);
            $region_start = length($source) if $region_start < 0;
            $region_start++ if $region_start < length($source);
            my $region_end = index($source, ";", $region_start);
            $region_end = length($source) if $region_end < 0;
            my $first_line_end = index($source, "\n", $region_start);
            $first_line_end = length($source) if $first_line_end < 0;
            my $first_line = substr($source, $region_start, $first_line_end - $region_start);

            my $used = $first_line !~ /^\s*(?:\/\/|$)/;
            if ($used) {
                $used = 0;
                for my $finding (@source_findings) {
                    if ($finding->[0] >= $region_start && $finding->[0] <= $region_end) {
                        $used = 1;
                        last;
                    }
                }
            }
            push @findings, [$mark_start, $mark_line, "lint suppression mark has no fixed color"] unless $used;
        }

        @findings = sort { $a->[1] <=> $b->[1] || $a->[2] cmp $b->[2] } @findings;
        for my $finding (@findings) {
            print STDERR "$display:$finding->[1]: $finding->[2]\n";
        }
        exit(@findings ? 1 : 0);
    ' "$input" "$input" "$filtered"
    scan_status=$?
    rm -f "$filtered"
    trap - EXIT HUP INT TERM
    return "$scan_status"
}

check_fixtures() {
    failed=0
    for fixture in "$fixture_dir"/pass/*; do
        if [ ! -f "$fixture" ]; then
            echo "$fixture: pass fixture does not exist" >&2
            failed=1
            continue
        fi
        if ! scan_file "$fixture" >/dev/null 2>&1; then
            echo "$fixture: expected no error" >&2
            failed=1
        fi
    done
    for fixture in "$fixture_dir"/fail/*; do
        if [ ! -f "$fixture" ]; then
            echo "$fixture: fail fixture does not exist" >&2
            failed=1
            continue
        fi
        if scan_file "$fixture" >/dev/null 2>&1; then
            echo "$fixture: expected an error" >&2
            failed=1
        fi
    done
    if scan_file "$fixture_dir/missing-fixture.c" >/dev/null 2>&1; then
        echo "$fixture_dir/missing-fixture.c: expected a read error" >&2
        failed=1
    fi
    return "$failed"
}

if [ "${1-}" = "--scan" ]; then
    shift
    overall_status=0
    for path in "$@"; do
        scan_file "$path" || overall_status=1
    done
    exit "$overall_status"
fi

check_fixtures || exit 1

overall_status=0
for path in "$source_dir"/*.c "$source_dir"/*.h; do
    scan_file "$path" || overall_status=1
done

if [ "$overall_status" -ne 0 ]; then
    echo "Fixed colors need a lint suppression mark." >&2
    exit 1
fi

echo "Color call check passed"
