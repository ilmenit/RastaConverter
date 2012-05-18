/* CommandLineParser.h: interface for the CommandLineParser class.

This code was written by Mohammed Lokhandwala. (lokhandwalamohammed@yahoo.com)

You may use this code in your projects provided the project is for non commercial use. 
You (or your company) should not be profiting from it. 

For commercial use, a written permission from the author is required.

This code is given without any warranties. You should use it at your own risk. Author is NOT
liable for any damage caused due to use of this code even if incidental.
*/
#if !defined(AFX_COMMANDLINEVALUEPAIRS_H__C9352603_89D1_4382_B303_6367A5FCB40B__INCLUDED_)
#define AFX_COMMANDLINEVALUEPAIRS_H__C9352603_89D1_4382_B303_6367A5FCB40B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#pragma warning (disable:4786)

// Standard Libs


// Epoch Headers
#include "EpochTypes.h"

using namespace std;
using namespace Epoch::Types;

/*
Parses command line. Every element of the command line must be seperated by space. 
Additional interpretation is as follows:

defined on CL as "/a=b" = pair
defined on CL as "/a"	= switch
defined on CL as "a"	= non-interpreted

*/

namespace Epoch
{
	namespace Foundation
	{
		class CommandLineParser  
		{
		public:
			virtual bool CommandLineParser::verifyCompulsory(vectorOfString pairs	= vectorOfString(),
												 vectorOfString switches			= vectorOfString(),
												 vectorOfString nonInterpreted		= vectorOfString());

			virtual bool nonInterpretedExists(string name) ;
			virtual bool switchExists(string name) ;
			virtual string getValue(string name, string defaultValue) ;
			virtual void parse(int argc, char * argv[]);
			CommandLineParser();

			virtual ~CommandLineParser();


			int mn_NonInterpreted;
			int mn_Switches;
			int mn_PairCount;
			string mn_command_line;


		protected:
			mapOfStringToString mMap_nvPairs;			// defined on CL as "/a=b"
			vectorOfString		mVec_Switches;			// defined on CL as "/a"
			vectorOfString		mVec_NonInterpreted;	// defined on CL as "a"


		};
	}
}
#endif // !defined(AFX_COMMANDLINEVALUEPAIRS_H__C9352603_89D1_4382_B303_6367A5FCB40B__INCLUDED_)
