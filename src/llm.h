/**
 * llm.h - 云端大模型 API 客户端 (多供应商)
 *
 *  支持 5 家: Z.ai / 智谱BigModel / DeepSeek / Moonshot / OpenAI
 *  统一 OpenAI 兼容协议
 */
#pragma once
#include "config.h"

struct LLMProvider {
    String name;
    String endpoint;
    String apiKey;
    String model;
};

struct LLMResult {
    bool   ok;
    String content;       // AI 回复文本
    int    httpCode;      // HTTP 状态码
    String errorMsg;      // 失败原因
};

// 供应商数量 + 列表
const int LLM_PROVIDER_COUNT = 6;
extern LLMProvider providers[LLM_PROVIDER_COUNT];
extern int currentProviderIdx;

// 获取/设置当前供应商
String llm_get_provider_name();
int    llm_get_provider_idx();

// 切换供应商 (idx 超范围则返回 false)
bool   llm_set_provider(int idx);

// 按名称切换 (支持模糊匹配, 如 "zai" "deep")
bool   llm_set_provider_by_name(const String& name);

// 切换到下一个非空 key 的供应商 (用于 Switch 按钮)
bool   llm_next_provider();

// 当前供应商的 API key 是否已配置 (非空)
bool   llm_current_provider_ready();

// 同步请求: 发送 prompt, 等待完整回复
LLMResult llm_chat(const String& userPrompt);

// 带最近对话上下文的同步请求
LLMResult llm_chat_with_context(const String& userPrompt, const String& context);
