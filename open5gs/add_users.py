#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

import bson
import click
import copy
import pymongo
import random
import sys

from misc.db.python.Open5GS import Open5GS


def add_user(
    imsi,
    key="00112233445566778899aabbccddeeff",
    op=None,
    opc="63bfa50ee6523365ff14c1f45f88737d",
    amf="9001",
    apn="internet",
    qci="9",
    ip_alloc="",
    sst=1,
    sd="",
    extra_slices="",
):
    """Add UE data to Open5GS mongodb"""

    if op is not None:
        opc = None

    slice_data = [
        {
            "sst": int(sst),
            "default_indicator": True,
            "session": [
                {
                    "name": apn,
                    "type": 3,
                    "pcc_rule": [],
                    "ambr": {"uplink": {"value": 1, "unit": 3}, "downlink": {"value": 1, "unit": 3}},
                    "qos": {
                        "index": int(qci),
                        "arp": {"priority_level": 8, "pre_emption_capability": 1, "pre_emption_vulnerability": 1},
                    },
                    "ue": {"ipv4": ip_alloc},
                },
                {
                    "name": "ims",
                    "type": 3,
                    "pcc_rule": [],
                    "ambr": {"uplink": {"value": 1, "unit": 3}, "downlink": {"value": 1, "unit": 3}},
                    "qos": {
                        "index": 5,
                        "arp": {"priority_level": 8, "pre_emption_capability": 1, "pre_emption_vulnerability": 1},
                    },
                },
            ],
        }
    ]

    sd_text = str(sd).strip()
    if sd_text:
        sd_value = int(sd_text, 16)
        if sd_value > 0xFFFFFF:
            raise ValueError(f"SD exceeds 24 bits: {sd_text}")
        slice_data[0]["sd"] = f"{sd_value:06x}"

    # Optional additional S-NSSAIs for the same UE.
    # Format: "2:000002|3:000003"
    for spec in str(extra_slices).strip().split("|"):
        spec = spec.strip()
        if not spec:
            continue

        parts = spec.split(":", 1)
        extra_sst = int(parts[0])
        extra_sd_text = parts[1].strip() if len(parts) == 2 else ""

        extra_slice = {
            "sst": extra_sst,
            "default_indicator": False,
            "session": copy.deepcopy(slice_data[0]["session"]),
        }

        if extra_sd_text:
            extra_sd = int(extra_sd_text, 16)
            if extra_sd > 0xFFFFFF:
                raise ValueError(
                    f"SD exceeds 24 bits: {extra_sd_text}"
                )
            extra_slice["sd"] = f"{extra_sd:06x}"

        slice_data.append(extra_slice)

    sub_data = {
        "imsi": imsi,
        "subscribed_rau_tau_timer": 12,
        "network_access_mode": 2,
        "subscriber_status": 0,
        "access_restriction_data": 32,
        "slice": slice_data,
        "ambr": {"uplink": {"value": 1, "unit": 3}, "downlink": {"value": 1, "unit": 3}},
        "security": {"k": key, "amf": amf, "op": op, "opc": opc},
        "schema_version": 1,
        "__v": 0,
    }

    return sub_data


def read_from_db(db_file):
    """Read UE data from subscriber CSV.

    8 fields  : legacy
    10 fields : one S-NSSAI
    11 fields : one primary + additional S-NSSAIs

    extra_slices format:
        2:000002|3:000003
    """
    subscriber_db = []

    try:
        fh = open(db_file, "r")
    except FileNotFoundError as e:
        print(f"Error reading subscriber_db.csv: {e}")
        return None

    for raw_line in fh:
        line = raw_line.strip()

        if not line or line.startswith("#"):
            continue

        fields = [x.strip() for x in line.split(",")]

        if len(fields) == 8:
            name, imsi, key, op_type, op_c, amf, qci, ip_alloc = fields
            sst, sd, extra_slices = "1", "", ""

        elif len(fields) == 10:
            name, imsi, key, op_type, op_c, amf, qci, ip_alloc, sst, sd = fields
            extra_slices = ""

        elif len(fields) == 11:
            name, imsi, key, op_type, op_c, amf, qci, ip_alloc, sst, sd, extra_slices = fields

        else:
            print(
                f"Invalid subscriber CSV row: expected 8, 10 or 11 fields, "
                f"got {len(fields)}: {line}"
            )
            return None

        if op_type.lower() == "op":
            op = op_c
            opc = None
        elif op_type.lower() == "opc":
            op = None
            opc = op_c
        else:
            print(f"Invalid OP type '{op_type}' for IMSI {imsi}")
            return None

        subscriber_db.append(
            {
                "imsi": imsi,
                "key": key,
                "op": op,
                "opc": opc,
                "amf": amf,
                "qci": qci,
                "ip_alloc": ip_alloc,
                "sst": int(sst),
                "sd": sd,
                "extra_slices": extra_slices,
            }
        )

    fh.close()
    return subscriber_db


def read_from_string(sub_data):
    """
    Read UE data from subscriber data string.
    Example string: "001010123456780,00112233445566778899aabbccddeeff,opc,63bfa50ee6523365ff14c1f45f88737d,8000,9,10.45.1.2"
    """

    subscriber_db = []

    try:
        imsi, key, op_type, op_c, amf, qci, ip_alloc = sub_data.split(",")
    except ValueError as e:
        print(f"Error reading subscriber string: {e}")
        return None

    opc = op_c
    op = None
    if op_type == "op":
        op = op_c
        opc = None

    subscriber_db.append(
        {"imsi": imsi, "key": key, "op": op, "opc": opc, "amf": amf, "qci": qci, "ip_alloc": ip_alloc.rstrip()}
    )

    return subscriber_db


@click.command()
@click.option("--mongodb", default="127.0.0.1", help="IP address or hostname of the mongodb instance.")
@click.option("--mongodb_port", default=27017, help="Port to connect to the mongodb instance.")
@click.option(
    "--subscriber_data",
    default="001010123456780,00112233445566778899aabbccddeeff,opc,63bfa50ee6523365ff14c1f45f88737d,8000,9,10.45.1.2",
    help="Single subscriber data string or full path to subscriber data csv-file.",
)
def main(mongodb, mongodb_port, subscriber_data):

    open5gs_client = Open5GS(mongodb, mongodb_port)

    if subscriber_data.endswith(".csv"):
        print("Reading subscriber data from csv-file.")
        subscriber_db = read_from_db(subscriber_data)
    else:
        print("Reading subscriber data from cmd.")
        subscriber_db = read_from_string(subscriber_data)

    if not subscriber_db:
        return sys.exit(1)

    for ue in subscriber_db:
        try:
            sub_data = add_user(**ue)
            # Add Subscriber using dict of sub_data
            print(open5gs_client.AddSubscriber(sub_data))
        except pymongo.errors.DuplicateKeyError:
            print(f"UE (IMSI={ue['imsi']}) already exists, updating it.")
            sub_data = add_user(**ue)
            # Update Subscriber using dict of sub_data
            print(open5gs_client.UpdateSubscriber(ue["imsi"], sub_data))


if __name__ == "__main__":
    main()
