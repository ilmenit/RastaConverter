/*CommandLineParser.cpp: implementation of the CommandLineParser class.

This code was written by Mohammed Lokhandwala. (lokhandwalamohammed@yahoo.com)

You may use this code in your projects provided the project is for non commercial use. 
You (or your company) should not be profiting from it. 

For commercial use, a written permission from the author is required.

This code is given without any warranties. You should use it at your own risk. Author is NOT
liable for any damage caused due to use of this code even if incidental.

*/

#include "CommandLineParser.h"
#include "port.h"

#include<algorithm>
#include <string.h>

namespace Epoch
{
	namespace Foundation
	{

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

		CommandLineParser::CommandLineParser()
		{
			mn_PairCount		= 0;
			mn_NonInterpreted	= 0;
			mn_Switches			= 0;
		}

		CommandLineParser::~CommandLineParser()
		{

		}

		void CommandLineParser::parse(int argc, char *argv[])
		{
			for(int i = 1; i < argc; i++) // skip filename, then pick each space seperated pair
			{
				string token = argv[i];
				if (i!=1)
					mn_command_line+=" ";
				mn_command_line+=token;

				if(token[0] != '/'){	// non interpreted
					mn_NonInterpreted++;
					mVec_NonInterpreted.push_back(token);
					continue;
				}
				else if(token.find_first_of('=') == string::npos){ // not a pair, and not interpreted so a switch

					mn_Switches++;
					mVec_Switches.push_back(token.substr(1)); // remove the '/'
					continue;
				}
				else // '/' and '=' was found
				{
					string		name;
					string		value;
					int			breakAt;

					token = token.substr(1);

					breakAt = token.find_first_of('=');

					name  = token.substr(0,breakAt);
					value = token.substr(breakAt+1);

                    // Remove surrounding quotes if present
                    if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() >= 2) {
                        value = value.substr(1, value.size() - 2);
                    }

					mn_PairCount++;
					mMap_nvPairs[name] = value;

					continue;
				}


			}
		}

		string CommandLineParser::getValue(string name, string defaultValue) 
		{
			string value = mMap_nvPairs[name];

			if(value == "")
				return defaultValue;
			else
				return value;

		}


		bool CommandLineParser::switchExists(string name) 
		{
			
			vectorOfString::iterator i = mVec_Switches.begin();

			for(;i != mVec_Switches.end(); i++){

				if(strcasecmp(i->c_str(), name.c_str()) == 0)	// case insensitive search;
					return true;

			}

			return false; 


		}

		bool CommandLineParser::nonInterpretedExists(string name) 
		{

			vectorOfString::iterator i = mVec_NonInterpreted.begin();

			for(;i != mVec_NonInterpreted.end(); i++){

				if(strcasecmp(i->c_str(), name.c_str()) == 0)	// case insensitive search;
					return true;

			}

			return false; 

		}


		bool CommandLineParser::verifyCompulsory(vectorOfString pairs,
												 vectorOfString switches,
												 vectorOfString nonInterpreted)
		{
			vectorOfString::iterator iP = pairs.begin();
			vectorOfString::iterator iPn = pairs.end();

			for(;iP != iPn; iP++)
				if(mMap_nvPairs[*iP] == "") return false;


			vectorOfString::iterator iS		= switches.begin();
			vectorOfString::iterator iSn	= switches.end();
			vectorOfString::iterator iSd	= mVec_Switches.begin();
			vectorOfString::iterator iSdn	= mVec_Switches.end();



			for(;iS != iSn; iS++)
				if(find(iSd, iSdn,*iS) == iSdn) return false;



			vectorOfString::iterator iN		= nonInterpreted.begin();
			vectorOfString::iterator iNn	= nonInterpreted.begin();
			vectorOfString::iterator iNd	= mVec_NonInterpreted.begin();
			vectorOfString::iterator iNdn	= mVec_NonInterpreted.end();

			for(;iN != iNn; iN++)
				if(find(iNd, iNdn,*iN) == iNdn) return false;



			return true;
		}





	}
}



