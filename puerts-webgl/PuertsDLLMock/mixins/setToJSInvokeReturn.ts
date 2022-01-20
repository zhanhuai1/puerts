import { FunctionCallbackInfoPtrManager, jsFunctionOrObjectFactory, makeBigInt, PuertsJSEngine } from "../library";

/**
 * mixin
 * JS调用C#时，C#设置返回到JS的值
 * 
 * @param engine 
 * @returns 
 */
export default function WebGLBackendSetToJSInvokeReturnApi(engine: PuertsJSEngine) {
    return {
        ReturnClass: function (isolate: IntPtr, info: MockIntPtr, classID: int) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = engine.csharpObjectMap.classes[classID];
        },
        ReturnObject: function (isolate: IntPtr, info: MockIntPtr, classID: int, self: CSObjectID) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = engine.csharpObjectMap.findOrAddObject(self, classID);
        },
        ReturnNumber: function (isolate: IntPtr, info: MockIntPtr, number: double) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = number;
        },
        ReturnString: function (isolate: IntPtr, info: MockIntPtr, strString: CSString) {
            const str = engine.unityApi.Pointer_stringify(strString);
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = str;
        },
        ReturnBigInt: function (isolate: IntPtr, info: MockIntPtr, longLow: number, longHigh: number) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = makeBigInt(longLow, longHigh);
        },
        ReturnBoolean: function (isolate: IntPtr, info: MockIntPtr, b: bool) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = b;
        },
        ReturnDate: function (isolate: IntPtr, info: MockIntPtr, date: double) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = new Date(date);
        },
        ReturnNull: function (isolate: IntPtr, info: MockIntPtr) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = null;
        },
        ReturnFunction: function (isolate: IntPtr, info: MockIntPtr, JSFunctionPtr: JSFunctionPtr) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            const jsFunc = jsFunctionOrObjectFactory.getJSFunctionById(JSFunctionPtr);
            callbackInfo.returnValue = jsFunc._func;
        },
        ReturnJSObject: function (isolate: IntPtr, info: IntPtr, JSObject: IntPtr) {
            throw new Error('not implemented')
        },
        ReturnArrayBuffer: function (isolate: IntPtr, info: MockIntPtr, /*byte[] */bytes: number, Length: int) {
            var callbackInfo = FunctionCallbackInfoPtrManager.GetByMockPointer(info);
            callbackInfo.returnValue = new Uint8Array(engine.unityApi.HEAP8.buffer, bytes, Length);
        },

    }
}