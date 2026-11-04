# Station data

The three CSV files are the OurAirports open data dumps, fetched on
2026-11-03 from https://ourairports.com/data/ (daily copies at
https://github.com/davidmegginson/ourairports-data):

- `airports.csv`             every airport, heliport and seaplane base, with ICAO, IATA and local codes and position
- `airport-frequencies.csv`  the frequencies of each airport (TWR, GND, ATIS, CTAF, UNICOM, APP, DEP, AWOS ...)
- `navaids.csv`              VOR, VOR-DME, VORTAC, DME, TACAN and NDB stations with frequency and position

OurAirports releases the data to the public domain: "All data is released
to the Public Domain, and comes with no guarantee of accuracy or fitness
for use." Check a frequency against a current chart or supplement before
relying on it in the air.

`make data` fetches fresh copies. The programs look for the folder in
`$FTA850_DATA`, then `data` next to the executable or in the current
folder, then `~/.local/share/fta850/data`.
