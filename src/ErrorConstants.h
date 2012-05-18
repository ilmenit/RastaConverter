/*
This code was written by Mohammed Lokhandwala. (lokhandwalamohammed@yahoo.com)

You may use this code in your projects provided the project is for non commercial use. 
You (or your company) should not be profiting from it. 

For commercial use, a written permission from the author is required.

This code is given without any warranties. You should use it at your own risk. Author is NOT
liable for any damage caused due to use of this code even if incidental.

*/
namespace Epoch
{
	namespace ErrorConstants
	{
		enum ProcessErrors {Success = 0,
							// Parameters
							UnhandledException = 100,
							InvalidInput,

							//IO
							SomeIOFailure = 200,
							FileReadFailure ,
							FileWriteFailure,

							
							// Others
							Unknown = 9999};
	}


	namespace Exceptions
	{
		class GenericOperationFailure{};
	}
}