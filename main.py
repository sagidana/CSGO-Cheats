from requests import get
import json
import re


def get_offsets():
    offsets = None
    try:
        r = get("https://raw.githubusercontent.com/frk1/hazedumper/master/csgo.json")
        offsets = json.loads(r.text)
    finally:
        return offsets

def update_source(path, client_state, entity_list, view_matrix):
    source = None
    with open(path, 'r') as main:
        source = main.read()

    source = re.sub("OFFSET_ENTITY_LIST.*\(0x[0-9a-zA-Z]+\)", f"OFFSET_ENTITY_LIST\t({entity_list})", source)
    source = re.sub("OFFSET_VIEW_MATRIX.*\(0x[0-9a-zA-Z]+\)", f"OFFSET_VIEW_MATRIX\t({view_matrix})", source)
    source = re.sub("OFFSET_CLIENT_STATE.*\(0x[0-9a-zA-Z]+\)", f"OFFSET_CLIENT_STATE\t({client_state})", source)

    with open(path, 'w') as main:
        main.write(source)

    


if __name__=='__main__':
    try:
        offsets = get_offsets()

        client_state = hex(offsets['signatures']['dwClientState'])
        entity_list = hex(offsets['signatures']['dwEntityList'])
        view_matrix = hex(offsets['signatures']['dwViewMatrix'])

        update_source("main.c", client_state, entity_list, view_matrix)
    except Exception as e:
        print(f"Exception: {e}")

