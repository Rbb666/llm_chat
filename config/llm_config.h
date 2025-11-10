#ifndef LLM_CONFIG_H__
#define LLM_CONFIG_H__

#include <rtthread.h>

#define LLM_CFG_API_KEY_MAX     128
#define LLM_CFG_MODEL_MAX       128
#define LLM_CFG_API_URL_MAX     128

#ifndef PKG_LLM_DEFAULT_API_URL
#if defined(PKG_LLM_USING_QWEN_CLOUD) && defined(PKG_LLM_QWEN_API_URL)
#define PKG_LLM_DEFAULT_API_URL PKG_LLM_QWEN_API_URL
#elif defined(PKG_LLM_USING_DOUBAO_CLOUD) && defined(PKG_LLM_DOUBAO_API_URL)
#define PKG_LLM_DEFAULT_API_URL PKG_LLM_DOUBAO_API_URL
#elif defined(PKG_LLM_USING_DEEPSEEK_CLOUD) && defined(PKG_LLM_DEEPSEEK_API_URL)
#define PKG_LLM_DEFAULT_API_URL PKG_LLM_DEEPSEEK_API_URL
#else
#define PKG_LLM_DEFAULT_API_URL ""
#endif
#endif

typedef struct llm_runtime_config
{
    char api_key[LLM_CFG_API_KEY_MAX];
    char model_name[LLM_CFG_MODEL_MAX];
    char api_url[LLM_CFG_API_URL_MAX];
    rt_bool_t is_configured;
} rt_align(RT_ALIGN_SIZE) llm_runtime_config_t;

llm_runtime_config_t *llm_config_get(void);

#endif /* LLM_CONFIG_H__ */
