
import csv
import sys
import json

def main(args):
    with open(args[1]) as fp:
        out = csv.writer(open(args[2], 'w'))

        format_dict = json.load(fp)

        for key in sorted(format_dict):
            value = format_dict[key]
            out.writerow((value['title'], key, value['description']))

if __name__ == "__main__":
    sys.exit(main(sys.argv))
