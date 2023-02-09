"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT

Scrapes metrics from CTest xml files and creates csv formatted files.
"""
import os.path
import sys
import csv
import argparse
import xml.etree.ElementTree as xmlElementTree
import datetime

from common import logging

DEFAULT_CTEST_LOG_FILENAME = 'Test.xml'
TAG_FILE = 'TAG'
TESTING_DIR = 'Testing'


def _get_default_csv_filename():
    # Format default file name based off of date
    now = datetime.datetime.now()
    return f"{now.year}_{now.month:02d}_{now.day:02d}.csv"


# Setup logging.
logger = logging.get_logger("test_metrics")
logging.setup_logger(logger)

# Create the csv field header
CTEST_FIELDS_HEADER = [
    'test_name',
    'status',
    'duration_seconds'
]


def main():
    # Parse args
    args = parse_args()

    # Construct the full path to the xml file
    file_path = _get_test_xml_path(args.build_folder, args.ctest_log)

    # Create csv file
    if os.path.exists(args.csv_file):
        logger.warning(f"The file {args.csv_file} already exists. It will be overriden.")
    with open(args.csv_file, 'w', encoding='UTF8', newline='') as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=CTEST_FIELDS_HEADER, restval='N/A')
        writer.writeheader()

        # Parse CTest xml and write to csv file
        parse_ctest_xml_to_csv(file_path, writer)


def parse_args():
    parser = argparse.ArgumentParser(
        description='This script assumes that a CTest xml file has been produced via the -T Test CTest option. The file'
                    'should exist inside of the build directory. The xml file will be parsed and write to a csv file.')
    parser.add_argument(
        'build_folder',
        help="Path to a CMake build folder (generated by running cmake)."
    )
    parser.add_argument(
        "-cl", "--ctest-log", action="store", default=DEFAULT_CTEST_LOG_FILENAME,
        help=f"The file name for the CTest output log (defaults to '{DEFAULT_CTEST_LOG_FILENAME}').",
    )
    parser.add_argument(
        "--csv-file", action="store", default=_get_default_csv_filename(),
        help=f"The directory and file name for the csv to be saved (defaults to YYYY_MM_DD)."
    )
    args = parser.parse_args()
    return args


def _get_test_xml_path(build_path, xml_file):
    # type: (str, str) -> str
    """
    Retrieves the full path to the CTest xml file. The xml file is produced in a folder that is defined by timestamp.
    This timestamp is defined as the first line in the CTest TAG file. The files are assumed to be created by CTest in
    the <build_path>//Testing directory.

    :param build_path: The full path to the cmake build folder
    :param xml_file: The name of the xml file
    :return: The full path to the xml file
    """
    full_tag_path = os.path.join(build_path, TESTING_DIR, TAG_FILE)
    if not os.path.exists(full_tag_path):
        raise FileNotFoundError(f"Could not find CTest TAG file at: {full_tag_path}")

    with open(full_tag_path, 'r') as tag_file:
        # The first line of the TAG file is the name of the folder
        line = tag_file.readline()
        if not line:
            raise EOFError("The CTest TAG file did not contain the name of the xml folder")
        folder_name = line.strip()

    xml_full_path = os.path.join(build_path, TESTING_DIR, folder_name, xml_file)
    if not os.path.exists(xml_full_path):
        raise FileNotFoundError(f'Unable to find CTest output log at: {xml_full_path}.')

    return xml_full_path


def parse_ctest_xml_to_csv(full_xml_path, writer):
    # type (str, dict, DictWriter) -> None
    """
    Parses the CTest xml file and writes the data to a csv file. Each test result will be written as a separate line.
    The structure of the CTest xml is assumed to be as followed:
    <?xml version="1.0" encoding="UTF-8"?>
    <Site>
        <Testing>
            <Test Status=...>
                <Name>...</Name>
                <Path>...</Path>
                <FullName>...</FullName>
                <FullCommandLine>...</FullCommandLine>
                <Results>
                    <NamedMeasurement type="numeric/double" name="Execution Time">
                        <Value>...</Value>
                    </NamedMeasurement>
                    <NamedMeasurement type="numeric/double" name="Processors">
                        <Value>...</Value>
                    </NamedMeasurement>
                    <NamedMeasurement type="text/string" name="Completion Status">
                        <Value>...</Value>
                    </NamedMeasurement>
                    <NamedMeasurement type="text/string" name="Command Line">
                        <Value>...</Value>
                    </NamedMeasurement>
                    <Measurement>
                        <Value encoding="base64" compression="gzip"...</Value>
                    </Measurement>
                </Results>
                <Labels>
                    <Label>SUITE_smoke</Label>
                    <Label>COMPONENT_foo</Label>
                    <Label>...</Label>
                </Labels>
            </Test>
            <Test Status="passed">
                ...
            </Test>
        </Testing>
    </Site>


    :param full_xml_path: The full path to the xml file
    :param writer: The DictWriter object to write to the csv file.
    :return: None
    """
    xml_root = xmlElementTree.parse(full_xml_path).getroot()

    # Each CTest test module will have a Test entry
    try:
        for test in xml_root.findall('./Testing/Test'):
            test_data_dict = {}
            # Get test execution time
            test_time = 0
            # There are many NamedMeasurements, but we need the one for Execution Time
            for measurement in test.findall('Results/NamedMeasurement'):
                if measurement.attrib['name'] == 'Execution Time':
                    test_time = float(measurement.find('Value').text)

            # Create a dict/json format to write to csv file
            test_data_dict['test_name'] = test.find('Name').text
            test_data_dict['status'] = test.attrib['Status']
            test_data_dict['duration_seconds'] = test_time

            writer.writerow(test_data_dict)
    except KeyError as exc:
        logger.exception(f"KeyError when parsing xml file: {full_xml_path}. Check xml keys for changes.", exc)


if __name__ == "__main__":
    sys.exit(main())