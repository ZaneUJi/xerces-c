/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * $Id$
 *
 */


// ---------------------------------------------------------------------------
//  Includes
// ---------------------------------------------------------------------------
#include    <xercesc/util/PlatformUtils.hpp>
#include    <xercesc/util/XMLString.hpp>
#include    <xercesc/util/XMLURL.hpp>
#include    <xercesc/util/XMLNetAccessor.hpp>
#include    <xercesc/util/BinInputStream.hpp>
#include    <xercesc/util/ValueArrayOf.hpp>

#include    <string.h>

#if defined(XERCES_NEW_IOSTREAMS)
#include	<iostream>
#else
#include	<iostream.h>
#endif


XERCES_CPP_NAMESPACE_USE


inline XERCES_STD_QUALIFIER ostream& operator<<(XERCES_STD_QUALIFIER ostream& os, const XMLCh* xmlStr)
{
	char* transcoded = XMLString::transcode(xmlStr);
    os << transcoded;
    XMLString::release(&transcoded);
    return os;
}


void
exercise(BinInputStream& stream)
{
	static float percents[] = { 1.0, 0.5, 0.25, 0.1, 0.15, 0.113, 0.333, 0.0015, 0.0013 };
	int numPercents = sizeof(percents) / sizeof(float);
	
	const unsigned int bufferMax = 4096;
	XMLByte buffer[bufferMax];

	int iteration = 0;
	unsigned int bytesRead = 0;
	do {
		// Calculate a percentage of our maximum buffer size, going through
		// them round-robin
		float percent = percents[iteration % numPercents];
		unsigned int bufCnt = (unsigned int)(bufferMax * percent);
		
		// Check to make sure we didn't go out of bounds
		if (bufCnt <= 0)
			bufCnt = 1;
		if (bufCnt > bufferMax)
			bufCnt = bufferMax;
		
		// Read bytes into our buffer
		bytesRead = stream.readBytes(buffer, bufCnt);
		//XERCES_STD_QUALIFIER cerr << "Read " << bytesRead << " bytes into a " << bufCnt << " byte buffer\n";

		if (bytesRead > 0)
		{
			// Write the data to standard out
			XERCES_STD_QUALIFIER cout.write((char*)buffer, bytesRead);
		}
		
		++iteration;
	} while (bytesRead > 0);
}

static void usage()
{
	XERCES_STD_QUALIFIER cerr << "Usage: NetAccessorTest [options] url\n"
			"\n"
			"This test reads data from the given url and writes the result\n"
			"to standard output.\n"
			"\n"
			"A variety of buffer sizes is are used during the test.\n"
			"\n"
			"Options:\n"
			"	   -H=xxx		 - Add an HTTP header.\n"
			"\n";
}

// ---------------------------------------------------------------------------
//  Program entry point
// ---------------------------------------------------------------------------
int
main(int argc, char** argv)
{
    // Init the XML platform
    try
    {
		static class Initializer
		{
		public:
			Initializer ()
			{
				XMLPlatformUtils::Initialize();
			}
			~Initializer()
			{
				XMLPlatformUtils::Terminate();
			}
		} dummy;
    }

    catch(const XMLException& toCatch)
    {
        XERCES_STD_QUALIFIER cout << "Error during platform init! Message:\n"
             << toCatch.getMessage() << XERCES_STD_QUALIFIER endl;
        return 1;
    }
    
    // Check command line and extract arguments.
    if (argc < 2)
    {
    	usage();
    	exit(1);
    }

	ValueArrayOf<char> headers(1);
	headers[0] = '\0';
    
    int parmInd;
    for (parmInd = 1; parmInd < argc; parmInd++)
    {
        // Break out on first parm not starting with a dash
        if (argv[parmInd][0] != '-')
            break;

        // Watch for special case help request
        if (!XMLString::compareString(argv[parmInd], "-?"))
        {
            usage();
            return 2;
        }
        else if (!XMLString::compareNString(argv[parmInd], "-H=", 3))
        {
            const char* const parm = &argv[parmInd][3];
			XMLSize_t length = headers.length();
			XMLSize_t parmLen = XMLString::stringLen(parm);
			headers.resize(length + parmLen + 2);
			memcpy(headers.rawData() + length - 1, parm, parmLen);
			memcpy(headers.rawData() + headers.length() - 3, "\r\n", 3);
        }
		else
		{
			break;
		}
    }

    if (parmInd + 1 != argc)
    {
        usage();
        return 1;
    }

    // Get the URL
    char* url = argv[parmInd];
    
    // Do the test
    try
    {
    	XMLURL xmlURL(url);
    	
		// Get the netaccessor
		XMLNetAccessor* na = XMLPlatformUtils::fgNetAccessor;
		if (na == 0)
		{
			XERCES_STD_QUALIFIER cerr <<  "No netaccessor is available. Aborting.\n";
			exit(2);
		}
		
		XMLNetHTTPInfo httpInfo;
		httpInfo.fHeaders = headers.rawData();
		httpInfo.fHeadersLen = headers.length() - 1;

		// Build a binary input stream
		BinInputStream* is = na->makeNew(xmlURL, &httpInfo);
		if (is == 0)
		{
			XERCES_STD_QUALIFIER cerr <<  "No binary input stream created. Aborting.\n";
			exit(3);
		}
		
		// Exercise the inputstream
		exercise(*is);
		
		// Delete the is
		delete is;
	
    }
    catch(const XMLException& toCatch)
    {
        XERCES_STD_QUALIFIER cout << "Exception during test:\n    "
             << toCatch.getMessage()
             << XERCES_STD_QUALIFIER endl;
    }

    return 0;
}

