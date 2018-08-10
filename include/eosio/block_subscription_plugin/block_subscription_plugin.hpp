#pragma once
#include <appbase/application.hpp>

namespace eosio {
	using namespace appbase;

	class block_subscription_plugin : public appbase::plugin<block_subscription_plugin> {
	public:
		block_subscription_plugin();
		virtual ~block_subscription_plugin();
	 
		APPBASE_PLUGIN_REQUIRES()
		virtual void set_program_options(options_description&, options_description& cfg) override;
	 
		void plugin_initialize(const variables_map& options);
		void plugin_startup();
		void plugin_shutdown();

	private:
		class block_subscription_plugin_impl* my;
	};
}
