# Parse Prez Log file
import argparse
from collections import namedtuple

CRLF = "\r\n"
entry = namedtuple('e',['idx','term','cmdname','cmd'])

def parse_log(content):
    while True:
        idx = content.find(CRLF)
        if idx == -1:
            return
        data_type = content[0]
        if data_type == "*":
            logentry, length = parse_arrays(content)
            content = content[length:]
            print logentry

def parse_arrays(content):
    idx = content.find(CRLF)
    count = int(content[1:idx])
    array = []
    start = idx + len(CRLF)
    for i in range(count):
        string, length = parse_bulk_string(content, start)
        start = length + len(CRLF)
        array.append(string)
    logentry = entry(*array)
    return logentry, start

def parse_bulk_string(content, start=0):
    idx = content.find(CRLF, start)
    if idx == -1:
        idx = start
    length = int(content[start + 1:idx])
    if length == -1:
        if idx + len(CRLF) == len(content):
            return None
        else:
            return None, idx
    else:
        data = content[idx + len(CRLF):idx + len(CRLF) + length]
        return data if start == 0 else [data, idx + len(CRLF) + length]

def main():
    parser = argparse.ArgumentParser(description='Parse Prez log file')
    parser.add_argument('filename', metavar='filename', type=argparse.FileType('r'),
                               help='Prez log file')
    args = parser.parse_args()
    parse_log(args.filename.read())

if __name__ == "__main__":
        main()


