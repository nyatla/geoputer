"""Independent XML/CSV consumers for files emitted by the production exporter."""
from pathlib import Path
import csv
import os
import xml.etree.ElementTree as ET


def check_exports(folder):
    folder = Path(folder) / 'export'
    base = '20261017_093012_0.gps.dat'
    ns = {'g': 'http://www.topografix.com/GPX/1/1', 'k': 'http://www.opengis.net/kml/2.2'}
    gpx = ET.parse(folder / (base + '.gpx')).getroot()
    assert gpx.attrib == {'version': '1.1', 'creator': 'Geoputer'}
    points = gpx.findall('g:trk/g:trkseg/g:trkpt', ns)
    assert len(points) == 25
    assert points[0].get('lat') == '35.681641'
    assert points[0].get('lon') == '140.027194'
    assert points[0].find('g:time', ns).text == '1970-01-01T00:00:00.000Z'
    assert points[1].find('g:time', ns).text == '2000-02-29T00:00:00.001Z'
    assert points[0].find('g:ele', ns).text == '-12.345'
    assert points[0].find('g:sat', ns).text == '12'
    assert points[0].find('g:hdop', ns).text == '0.80'
    assert [child.tag.rsplit('}', 1)[-1] for child in points[0]] == ['ele', 'time', 'sat', 'hdop']
    assert list(points[2]) == []  # Unknown values are absent, not zero-filled.
    kml = ET.parse(folder / (base + '.kml')).getroot()
    coordinates = kml.find('.//k:LineString/k:coordinates', ns).text.split()
    assert len(coordinates) == 25
    assert coordinates[0] == '140.027194,35.681641'  # KML uses lon,lat.
    single = ET.parse(folder / 'single.gps.dat.kml').getroot()
    assert single.find('.//k:Point/k:coordinates', ns) is not None
    with (folder / (base + '.csv')).open(newline='', encoding='utf-8') as source:
        rows = list(csv.DictReader(source))
    assert len(rows) == 25 and len(rows[0]) == 10
    assert rows[0]['speed_m_s'] == '10.00'
    assert rows[0]['course_deg'] == '140.25' and rows[0]['hdop'] == '0.80'
    assert rows[0]['utc_ms'] == '0' and rows[0]['utc'] == '1970-01-01T00:00:00.000Z'
    for field in ['utc', 'utc_ms', 'altitude_m', 'speed_m_s', 'course_deg', 'hdop', 'satellites']:
        assert rows[2][field] == ''
    schema = os.environ.get('GEOPUTER_GPX_SCHEMA')
    if schema:
        validate_gpx_schema(folder, schema)
    print('PASS: independent GPX/KML XML parsing, coordinate order, UTC/leap day, missing fields and CSV units')


def validate_gpx_schema(folder, schema_path):
    """Optional validation against the official GPX XSD using system libxml2."""
    import ctypes as c
    from ctypes.util import find_library
    xml = c.CDLL(find_library('xml2'))
    signatures = {
        'xmlSchemaNewParserCtxt': ([c.c_char_p], c.c_void_p),
        'xmlSchemaParse': ([c.c_void_p], c.c_void_p),
        'xmlSchemaNewValidCtxt': ([c.c_void_p], c.c_void_p),
        'xmlReadFile': ([c.c_char_p, c.c_char_p, c.c_int], c.c_void_p),
        'xmlSchemaValidateDoc': ([c.c_void_p, c.c_void_p], c.c_int),
        'xmlFreeDoc': ([c.c_void_p], None),
        'xmlSchemaFreeValidCtxt': ([c.c_void_p], None),
        'xmlSchemaFree': ([c.c_void_p], None),
        'xmlSchemaFreeParserCtxt': ([c.c_void_p], None),
    }
    for name, (args, result) in signatures.items():
        getattr(xml, name).argtypes = args
        getattr(xml, name).restype = result
    parser = xml.xmlSchemaNewParserCtxt(os.fsencode(schema_path))
    assert parser
    schema = xml.xmlSchemaParse(parser)
    assert schema
    validator = xml.xmlSchemaNewValidCtxt(schema)
    assert validator
    try:
        for path in folder.glob('*.gpx'):
            doc = xml.xmlReadFile(os.fsencode(path), None, 2048)  # XML_PARSE_NONET
            assert doc
            try:
                assert xml.xmlSchemaValidateDoc(validator, doc) == 0, path.name
            finally:
                xml.xmlFreeDoc(doc)
    finally:
        xml.xmlSchemaFreeValidCtxt(validator)
        xml.xmlSchemaFree(schema)
        xml.xmlSchemaFreeParserCtxt(parser)
    print('PASS: GPX 1.1 official XML Schema validation')
