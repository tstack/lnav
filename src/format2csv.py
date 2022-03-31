
import glob
import csv
import sys
import json

def main(args):
    with open(args[1], 'w') as ofp:
        out = csv.writer(ofp)
        for format_path in sorted(glob.glob("%s/*.json" % args[2])):
            with open(format_path) as fp:
                format_dict = json.load(fp)

                for key in sorted(format_dict):
                    value = format_dict[key]
                    if not isinstance(value, dict):
                        continue
                    if 'title' not in value:
                        raise Exception("format '%s' is missing 'title'" % key)
                    out.writerow((value['title'], key, value['description']))

if __name__ == "__main__":
    sys.exit(main(sys.argv))
