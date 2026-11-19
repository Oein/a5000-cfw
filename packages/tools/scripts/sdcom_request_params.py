"""libScalarDaemon 디컴파일(.c)에서 명령 처리기별 요청 파라미터(id, 타입 검사, 읽는 함수)를 추출.

근거 신뢰도: derived (디컴파일 텍스트의 getId()/getType 비교와 getData 호출 패턴).
출력: re/scalar/request_params.tsv  — msgId, 처리기, 파라미터 id 목록, 타입, 접근자
사용법: python -I scripts/sdcom_request_params.py re/ida/scalar/libScalarDaemon.so.c re/scalar/message_ids.tsv
"""
import re
import sys

TYPE_NAME = {1: 'Int', 2: 'Binary', 3: 'String', 5: 'IntArray', 6: 'StringArray'}


def main():
    src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    ids = {}
    for line in open(sys.argv[2]):
        p = line.rstrip('\n').split('\t')
        if len(p) >= 4:
            ids.setdefault(p[3], []).append(p[1])
    funcs = re.split(r'\n// ---- ', src)
    rows = {}
    for f in funcs:
        head = f.split('\n', 1)[0]
        m = re.search(r'CommandProcessor(\w+?)\d*(?:13SyncProcessor|14AsyncProcessor)?\d*19extractSDComMessage', head)
        if 'extractSDComMessage' not in head:
            continue
        m = re.search(r'_ZN12SCALARDAEMON\d+CommandProcessor(\w+?)\d+(?:Sync|Async)Processor', head)
        name = m.group(1) if m else head
        body = f
        pids = sorted({int(x) for x in re.findall(r'getId\([^)]*\)\s*[!=]=\s*(\d+)', body)} |
                      {int(x) for x in re.findall(r'case (\d+):', body) if 'getId' in body})
        types = sorted({int(x) for x in re.findall(r'\+ 16\)\)\([^)]*\)\s*[!=]=\s*(\d+)', body)})
        acc = sorted(set(re.findall(r'(SDComMsg\w+Param)::(getData\w*|getArrayLength|getDataAt)', body)))
        rows.setdefault(name, set())
        rows[name].add((tuple(pids), tuple(types), tuple('%s::%s' % a for a in acc)))
    with open('re/scalar/request_params.tsv', 'w') as o:
        o.write('msgIds\tprocessor\tparamIds\ttypes\taccessors\n')
        for name in sorted(rows):
            for pids, types, acc in sorted(rows[name]):
                o.write('%s\t%s\t%s\t%s\t%s\n' % (
                    ','.join(ids.get(name, ['?'])), name, ','.join(map(str, pids)),
                    ','.join(TYPE_NAME.get(t, str(t)) for t in types), ' '.join(acc)))
    print(len(rows), 'processors')


if __name__ == '__main__':
    main()
