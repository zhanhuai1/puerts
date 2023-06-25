struct MockV8Value
{
    int JSValueType;
    int FinalValuePointer;
    int length;
    int FunctionCallbackInfo;
};
struct MockV8NumberOrDate
{
    int JSValueType;
    float value;
    float value2;
    int FunctionCallbackInfo;
};

extern "C" {
    void* GetArgumentValue(void* infoptr, int index) 
    {
        int step = sizeof(int);
        return (void*)((long)infoptr + (index * 4 + 1) * step);
    }
    int GetArgumentType(void* isolate, void* infoptr, int index, bool isByRef)
    {
        int step = sizeof(int);
        MockV8Value *value = (MockV8Value*)((long)infoptr + (index * 4 + 1) * step);
        return value->JSValueType;
    }
    int GetJsValueType(void* isolate, MockV8Value* value, bool byref)
    {
        return value->JSValueType;
    }
    double GetNumberFromValue(void* isolate, MockV8NumberOrDate* value, bool byref)
    {
        return static_cast<double>(value->value);
    }
    double GetDateFromValue(void* isolate, MockV8NumberOrDate* value, bool byref)
    {
        return static_cast<double>(value->value);
    }
    void* GetStringFromValue(void* isolate, MockV8Value* value, int &length, bool byref)
    {
        length = value->length;
        return (void*)(int)value->FinalValuePointer;
    }
    bool GetBooleanFromValue(void* isolate, MockV8Value* value, bool byref)
    {
        return (bool)(int)value->FinalValuePointer;
    }
    int GetBigIntFromValue(void* isolate, MockV8Value* value, bool byref)
    {
        return (int)value->FinalValuePointer;
    }
    void* GetObjectFromValue(void* isolate, MockV8Value* value, bool byref)
    {
        return (void*)(int)value->FinalValuePointer;
    }
    void* GetFunctionFromValue(void* isolate, MockV8Value* value, bool byref)
    {
        return (void*)(int)value->FinalValuePointer;
    }
    void* GetJSObjectFromValue(void* isolate, MockV8Value* value, bool byref)
    {
        return (void*)(int)value->FinalValuePointer;
    }
    void* GetArrayBufferFromValue(void* isolate, MockV8Value* value, int &length, bool byref)
    {
        length = value->length;
        return (void*)(int)value->FinalValuePointer;
    }
}