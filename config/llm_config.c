#include "llm_config.h"

static llm_runtime_config_t g_llm_runtime_config =
{
    .api_key = PKG_LLM_API_KEY,
    .model_name = PKG_LLM_MODEL_NAME,
    .api_url = PKG_LLM_DEFAULT_API_URL,
    .is_configured = RT_TRUE
};

llm_runtime_config_t *llm_config_get(void)
{
    return &g_llm_runtime_config;
}
