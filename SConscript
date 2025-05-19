from building import *

cwd = GetCurrentDir()
path = [cwd]
src = Glob('*.c')

path += [cwd + '/ports']
src += Glob('ports/chat_port.c')
src += Glob('demo/llm_contory.c')

group = DefineGroup('llm', src, depend = ['PKG_USING_LLMCHAT'], CPPPATH = path)

Return('group')
