from building import *

cwd = GetCurrentDir()

src = []
path = [cwd]
path += [cwd + "/ports"]
path += [cwd + "/config"]

src += ["llm.c"]
src += ["config/llm_config.c"]
src += ["ports/chat_port.c"]

if GetDepend(["PKG_USING_LLMCHAT_DEMO"]):
    src += ["demo/llm_contory.c"]

if GetDepend(["PKG_LLMCHAT_WEBNET_MODE"]):
    src += ["ports/llm_webnet.c"]

group = DefineGroup("llm", src, depend=["PKG_USING_LLMCHAT"], CPPPATH=path)

Return("group")
