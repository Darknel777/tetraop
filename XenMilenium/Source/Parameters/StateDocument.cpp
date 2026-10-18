#include "StateDocument.h"

#include "ParameterRegistry.h"

namespace xenmilenium
{
namespace
{
juce::Identifier id(const char* name)
{
    return juce::Identifier(name);
}

juce::DynamicObject* objectOf(juce::var& value)
{
    return value.getDynamicObject();
}

const juce::DynamicObject* objectOf(const juce::var& value)
{
    return value.getDynamicObject();
}

juce::var emptyObject()
{
    return juce::var(new juce::DynamicObject());
}

juce::var emptyArray()
{
    return juce::var(juce::Array<juce::var>());
}

bool hasProperty(const juce::DynamicObject& object, const juce::Identifier& name)
{
    return object.getProperties().contains(name);
}

void copyProperties(const juce::DynamicObject& source, juce::DynamicObject& destination)
{
    for (int index = 0; index < source.getProperties().size(); ++index)
        destination.setProperty(source.getProperties().getName(index), source.getProperties().getValueAt(index));
}

const juce::AudioParameterBool* boolParameter(const juce::AudioProcessorValueTreeState& state, const ParameterSpec& spec)
{
    return dynamic_cast<const juce::AudioParameterBool*>(state.getParameter(spec.apvtsId));
}

const juce::AudioParameterChoice* choiceParameter(const juce::AudioProcessorValueTreeState& state, const ParameterSpec& spec)
{
    return dynamic_cast<const juce::AudioParameterChoice*>(state.getParameter(spec.apvtsId));
}

juce::var plainValue(const juce::AudioProcessorValueTreeState& state, const ParameterSpec& spec)
{
    if (spec.kind == ParameterKind::boolean)
    {
        const auto* parameter = boolParameter(state, spec);
        jassert(parameter != nullptr);
        return parameter->get();
    }

    const auto* parameter = choiceParameter(state, spec);
    jassert(parameter != nullptr);
    return parameter->getCurrentChoiceName();
}

float normalisedFor(const juce::AudioProcessorValueTreeState& state, const ParameterSpec& spec, const juce::var& plain)
{
    if (spec.kind == ParameterKind::boolean)
    {
        const auto* parameter = boolParameter(state, spec);
        return parameter->convertTo0to1(static_cast<bool>(plain) ? 1.0f : 0.0f);
    }

    const auto* parameter = choiceParameter(state, spec);
    const int index = ParameterRegistry::indexOfChoice(spec, plain.toString());
    return parameter->convertTo0to1(static_cast<float>(index));
}

bool plainIsValid(const ParameterSpec& spec, const juce::var& plain)
{
    if (plain.isVoid())
        return true;

    if (spec.kind == ParameterKind::boolean)
        return plain.isBool();

    return plain.isString() && ParameterRegistry::indexOfChoice(spec, plain.toString()) >= 0;
}
} // namespace

StoredDocument makeInitialDocument()
{
    auto* ui = new juce::DynamicObject();
    ui->setProperty(id("focusedPart"), 1);
    juce::Array<juce::var> editParts;
    editParts.add(1);
    ui->setProperty(id("editParts"), editParts);
    ui->setProperty(id("page"), juce::var());
    ui->setProperty(id("window"), juce::var());

    auto* unknown = new juce::DynamicObject();
    unknown->setProperty(id("parameters"), emptyObject());
    unknown->setProperty(id("document"), emptyObject());

    StoredDocument document;
    document.ui = juce::var(ui);
    document.embeddedWavetables = emptyArray();
    document.unknown = juce::var(unknown);
    return document;
}

juce::String serialiseDocument(const juce::AudioProcessorValueTreeState& state, const StoredDocument& document)
{
    auto* root = new juce::DynamicObject();
    auto* parameters = new juce::DynamicObject();

    for (const auto& spec : ParameterRegistry::all())
        parameters->setProperty(juce::Identifier(spec.statePath), plainValue(state, spec));

    root->setProperty(id("formatVersion"), 1);
    root->setProperty(id("ui"), document.ui);
    root->setProperty(id("parameters"), juce::var(parameters));
    root->setProperty(id("embeddedWavetables"), document.embeddedWavetables);
    root->setProperty(id("unknown"), document.unknown);

    return juce::JSON::toString(juce::var(root), false);
}

bool deserialiseDocument(const juce::String& json,
                         juce::AudioProcessorValueTreeState& state,
                         StoredDocument& document)
{
    juce::var parsed;
    const auto parsedResult = juce::JSON::parse(json, parsed);
    if (parsedResult.failed() || objectOf(parsed) == nullptr)
        return false;

    const auto& root = *objectOf(parsed);
    const auto versionName = id("formatVersion");
    if (! hasProperty(root, versionName))
        return false;

    const auto version = root.getProperty(versionName);
    if (! version.isInt() || static_cast<int>(version) != 1)
        return false;

    const auto parametersName = id("parameters");
    const juce::DynamicObject* incomingParameters = nullptr;
    if (hasProperty(root, parametersName))
    {
        const auto parametersVar = root.getProperty(parametersName);
        incomingParameters = objectOf(parametersVar);
        if (incomingParameters == nullptr)
            return false;
    }

    std::vector<std::pair<const ParameterSpec*, juce::var>> overrides;
    juce::var keptParametersVar(new juce::DynamicObject());
    juce::var keptDocumentVar(new juce::DynamicObject());
    auto* keptParameters = objectOf(keptParametersVar);
    auto* keptDocument = objectOf(keptDocumentVar);

    const auto unknownName = id("unknown");
    if (hasProperty(root, unknownName))
    {
        const auto unknownVar = root.getProperty(unknownName);
        const auto* unknownObject = objectOf(unknownVar);
        if (unknownObject == nullptr)
            return false;

        const auto parametersBagName = id("parameters");
        const auto documentBagName = id("document");
        if (hasProperty(*unknownObject, parametersBagName))
        {
            const auto bag = unknownObject->getProperty(parametersBagName);
            if (objectOf(bag) == nullptr)
                return false;
            copyProperties(*objectOf(bag), *keptParameters);
        }

        if (hasProperty(*unknownObject, documentBagName))
        {
            const auto bag = unknownObject->getProperty(documentBagName);
            if (objectOf(bag) == nullptr)
                return false;
            copyProperties(*objectOf(bag), *keptDocument);
        }

        for (int index = 0; index < unknownObject->getProperties().size(); ++index)
        {
            const auto name = unknownObject->getProperties().getName(index);
            if (name == parametersBagName || name == documentBagName)
                continue;
            keptDocument->setProperty(name, unknownObject->getProperties().getValueAt(index));
        }
    }

    if (incomingParameters != nullptr)
    {
        for (int index = 0; index < incomingParameters->getProperties().size(); ++index)
        {
            const auto path = incomingParameters->getProperties().getName(index).toString();
            const auto value = incomingParameters->getProperties().getValueAt(index);
            const auto* spec = ParameterRegistry::findByStatePath(path);

            if (spec == nullptr)
            {
                if (! value.isVoid())
                    keptParameters->setProperty(juce::Identifier(path), value);
                continue;
            }

            if (! plainIsValid(*spec, value))
                return false;

            if (! value.isVoid())
                overrides.emplace_back(spec, value);
        }
    }

    for (const auto& spec : ParameterRegistry::all())
        keptParameters->removeProperty(juce::Identifier(spec.statePath));

    const auto uiName = id("ui");
    juce::var nextUi = document.ui;
    if (hasProperty(root, uiName))
    {
        const auto ui = root.getProperty(uiName);
        if (objectOf(ui) == nullptr)
            return false;
        nextUi = ui;
    }

    const auto tablesName = id("embeddedWavetables");
    juce::var nextTables = emptyArray();
    if (hasProperty(root, tablesName))
    {
        const auto tables = root.getProperty(tablesName);
        if (! tables.isArray())
            return false;
        nextTables = tables;
    }

    for (int index = 0; index < root.getProperties().size(); ++index)
    {
        const auto name = root.getProperties().getName(index);
        if (name == versionName || name == parametersName || name == unknownName || name == uiName || name == tablesName)
            continue;
        if (name == id("notes") || name == id("openQuestions"))
            continue;
        keptDocument->setProperty(name, root.getProperties().getValueAt(index));
    }

    for (const auto& spec : ParameterRegistry::all())
    {
        auto* parameter = state.getParameter(spec.apvtsId);
        jassert(parameter != nullptr);
        parameter->setValue(parameter->getDefaultValue());
    }

    for (const auto& overrideValue : overrides)
    {
        auto* parameter = state.getParameter(overrideValue.first->apvtsId);
        parameter->setValue(normalisedFor(state, *overrideValue.first, overrideValue.second));
    }

    auto* unknown = new juce::DynamicObject();
    unknown->setProperty(id("parameters"), juce::var(keptParameters));
    unknown->setProperty(id("document"), juce::var(keptDocument));

    document.ui = nextUi;
    document.embeddedWavetables = nextTables;
    document.unknown = juce::var(unknown);
    return true;
}

} // namespace xenmilenium
