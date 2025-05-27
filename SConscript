from building import *

cwd = GetCurrentDir()

path = [cwd]
path += [cwd + '/ports']
src = []

src += ['ports/chat_port.c']
src +=['llm.c']

if GetDepend(['PKG_USING_LLMCHAT_DEMO_LLM_CONTORY']):
    src += ['demo/llm_contory.c']

group = DefineGroup('llm', src, depend = ['PKG_USING_LLMCHAT'], CPPPATH = path)

Return('group')
