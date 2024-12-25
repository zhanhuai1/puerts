/*
* Tencent is pleased to support the open source community by making Puerts available.
* Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
* Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may be subject to their corresponding license terms.
* This file is subject to the terms and conditions defined in file 'LICENSE', which is part of this source code package.
*/

using System;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using EpicGames.Core;

namespace CSharpParamDefaultValueMetas
{
    [UnrealHeaderTool]
    class CSharpParamDefaultValueMetas
    {
        [UhtExporter(Name = "Puerts", Description = "Puerts Default Values Collector", Options = UhtExporterOptions.Default, ModuleName = "JsEnv")]
        private static void UnLuaDefaultParamCollectorUbtPluginExporter(IUhtExportFactory factory)
        {
            var paramDefaultValueMetas = new CSharpParamDefaultValueMetas(factory);
            
            paramDefaultValueMetas.Generate();
            
            bool bHasGameRuntime = (from module in factory.Session.Modules where module.Module.ModuleType == UHTModuleType.GameRuntime select module).Any();
            paramDefaultValueMetas.OutputIfNeeded(bHasGameRuntime);
        }
        
        private IUhtExportFactory Factory;
        private BorrowStringBuilder BSB;
        private StringBuilder GeneratedContentBuilder => BSB.StringBuilder;

        public CSharpParamDefaultValueMetas(IUhtExportFactory factory)
        {
            Factory = factory;
            BSB = new BorrowStringBuilder(StringBuilderCache.Big);
        }
        
        IEnumerable<UhtType> TraverseTree(UhtType parentNode)
        {
            yield return parentNode;
            
            if (parentNode.Children != null)
            {
                foreach (UhtType childNode in parentNode.Children.SelectMany(TraverseTree))
                {
                    yield return childNode;
                }
            }
        }

        private void Generate()
        {
            GeneratedContentBuilder.Append("// Generated By C# UbtPlugin\r\n");
            
            var packages = (from module in Factory.Session.Modules 
                where (module.Module.ModuleType == UHTModuleType.EngineRuntime || module.Module.ModuleType == UHTModuleType.GameRuntime)
                select module.ScriptPackage);

                
            var classes = packages
                .SelectMany(TraverseTree)
                .Where(t => t is UhtClass).Cast<UhtClass>()
                .Where(c => !c.ClassFlags.HasAnyFlags(EClassFlags.Interface));

            classes.Distinct().OrderBy(c => c.SourceName).ToList().ForEach(ExportClass);
        }
        
        private void ExportClass(UhtClass uhtClass)
        {
            bool declared = false;
            
            Action declareOnce = () => {
                if (!declared)
                {
                    GeneratedContentBuilder.AppendFormat("PC = &ParamDefaultMetas.Add(TEXT(\"{0}\"));\r\n", uhtClass.EngineName);
                    declared = true;
                }
            };
            
            foreach (UhtFunction uhtFunction in uhtClass.Functions.OrderBy(Func => Func.SourceName))
            {
                if (!uhtFunction.MetaData.IsEmpty())
                {
                    ExportFunction(uhtFunction, declareOnce);
                }
            }
            
            if (declared)
            {
                GeneratedContentBuilder.Append("\r\n");
            }
        }
        
        private static bool TryGetDefaultValue(UhtMetaData metaData, UhtProperty property, out string value)
        {
            var hasValue = metaData.TryGetValue(property.SourceName, out string? tempValue);
            if (!hasValue)
            {
                var cppKey = "CPP_Default_" + property.SourceName;
                hasValue = metaData.TryGetValue(cppKey, out tempValue);
            }
            value = tempValue ?? string.Empty;
            return hasValue;
        }
        
        private void ExportFunction(UhtFunction uhtFunction, Action declareClassOnce)
        {
            bool declared = false;
            
            var properties = uhtFunction.Children
                .Where(c => c is UhtProperty)
                .Cast<UhtProperty>()
                .Where(p => p.PropertyFlags.HasAnyFlags(EPropertyFlags.Parm) && !p.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm));
            
            foreach (var property in properties.OrderBy(Property => Property.SourceName))
            {
                if (TryGetDefaultValue(uhtFunction.MetaData, property, out string defaultValue))
                {
                    declareClassOnce();
                    if (!declared)
                    {
                        GeneratedContentBuilder.AppendFormat("PF = &PC->Add(TEXT(\"{0}\"));\r\n", uhtFunction.SourceName);
                        declared = true;
                    }
                    
                    string escapeDefaultValue = defaultValue.Replace("\"", "\\\"");
                    if (property is UhtBoolProperty)
                    {
                        escapeDefaultValue = escapeDefaultValue.Replace("true", "True").Replace("false", "False");
                    }
                    GeneratedContentBuilder.AppendFormat("PF->Add(TEXT(\"{0}\"), TEXT(\"{1}\"));\r\n", property.SourceName, escapeDefaultValue);
                }
            }
        }
        
        private void OutputIfNeeded(bool bHasGameRuntime)
        {
            string filePath = Factory.MakePath("InitParamDefaultMetas", ".inl");
            
            if (File.Exists(filePath))
            {
                var fileContent = File.ReadAllText(filePath);
                if (bHasGameRuntime ? (!fileContent.Equals(GeneratedContentBuilder.ToString())) : (fileContent.Length != 0))
                {
                    Factory.CommitOutput(filePath, GeneratedContentBuilder);
                }
            }
            else
            {
                Factory.CommitOutput(filePath, GeneratedContentBuilder);
            }
        }
    }
}
