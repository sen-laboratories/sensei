/*
 * Markdown Extractor Plugin for SEN
 * Distributed under the terms of the MIT License.
 * (c) 2026 Gregor B. Rosenauer, SEN Labs <gregor.rosenauer@sen-labs.org>
 */

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <Path.h>
#include <String.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

// Core SEN includes
#include <sen/Sen.h>
#include <sen/Sensei.h>

const char* kApplicationSignature = "application/x-vnd.sen-labs.MarkdownExtractor";

// In-memory AST Node to track hierarchy before serializing
struct MarkdownNode {
    std::string label;
    int32 offset;
    int32 line;
    bool isSelf;
    std::vector<MarkdownNode*> children;

    ~MarkdownNode() {
        for (auto c : children) delete c;
    }
};

class MarkdownExtractorApp : public BApplication {
public:
    MarkdownExtractorApp();
    virtual void ArgvReceived(int32 argc, char** argv) override;
    virtual void RefsReceived(BMessage* message) override;

private:
    status_t ProcessMarkdown(const entry_ref* ref, bool isSelfRelation, BMessage* reply);
    void ExtractReferences(std::ifstream& stream, bool isSelfRelation, BMessage* reply);
    void SerializeNodes(const std::vector<MarkdownNode*>& siblings, BMessage* msg, bool isSelfRelation);
};

MarkdownExtractorApp::MarkdownExtractorApp()
    : BApplication(kApplicationSignature) {
}

void MarkdownExtractorApp::ArgvReceived(int32 argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Invalid usage, simply provide Markdown file as 1st argument." << std::endl;
        return;
    }

    int arg = 1;
    BMessage refsMsg(B_REFS_RECEIVED);

    // check for self relations flag
    // todo: use more elaborate options parser if needed
    if (argc > 2) {
        if (strncmp(argv[arg], SENSEI_OPTION_SELF, strlen(argv[arg])) == 0) {
            refsMsg.AddBool(SEN_RELATION_IS_SELF, true);
            arg++;
        }
        // check for other options - no others supported yet
        if (strncmp(argv[arg], "-", 1) == 0) {  // catches short - and long-form --
            std::cerr << "Unknown parameter " << argv[arg] << std::endl;
            return;
        }
    }

    BEntry entry(argv[arg]);
    entry_ref ref;

    if (entry.GetRef(&ref) == B_OK) {
        refsMsg.AddRef("refs", &ref);
        RefsReceived(&refsMsg);
    }
}

void MarkdownExtractorApp::RefsReceived(BMessage* message) {
    entry_ref ref;

    if (message->FindRef("refs", &ref) != B_OK) {
        BAlert* alert = new BAlert("Error launching SEN Markdown Extractor",
            "Failed to resolve source file.",
            "Oh no.");
        alert->SetFlags(alert->Flags() | B_WARNING_ALERT | B_CLOSE_ON_ESCAPE);
        alert->Go();
        return;
    }

    // scan self references like outline and internal links, or external refs?
    bool isSelfRelation = message->GetBool(SEN_RELATION_IS_SELF, false);

    BMessage reply(SENSEI_MESSAGE_RESULT);
    status_t result = ProcessMarkdown(&ref, isSelfRelation, &reply);

    reply.AddString(SENSEI_RESULT, strerror(result));

    // DEBUG
    reply.PrintToStream();

    // Send the structured array back to SEN before quitting
    message->SendReply(&reply, this);

    Quit();
}

status_t MarkdownExtractorApp::ProcessMarkdown(const entry_ref* ref, bool isSelfRelation, BMessage* reply) {
    BPath inputPath(ref);
    std::ifstream fileStream(inputPath.Path());

    if (!fileStream.is_open()) {
        reply->AddString("error", "Failed to open Markdown file");
        return B_ERROR;
    }

    // Pass the stream to the extraction logic
    ExtractReferences(fileStream, isSelfRelation, reply);

    return B_OK;
}

void MarkdownExtractorApp::ExtractReferences(std::ifstream& stream, bool isSelfRelation, BMessage* reply) {
    std::string lineStr;
    std::streampos currentOffset = 0;
    int32 currentLine = 1;

    std::vector<MarkdownNode*> roots;
    std::vector<std::pair<int, MarkdownNode*>> stack;

    std::regex headingRegex("^(\\#{1,6})\\s+(.*)");
    std::regex linkRegex("\\[([^\\]]+)\\]\\(([^)]+)\\)");
    std::regex wikiLinkRegex("\\[\\[(.*?)\\]\\]");

    while (std::getline(stream, lineStr)) {
        std::smatch match;

        // process structural hierarchy from headings for self relations only
        if (isSelfRelation) {
            if (std::regex_search(lineStr, match, headingRegex)) {
                int level = match[1].length();

                MarkdownNode* node = new MarkdownNode();
                node->label = match[2].str();
                node->offset = static_cast<int32>(currentOffset);
                node->line = currentLine;
                node->isSelf = true; // Outline nodes are always self-relations

                while (!stack.empty() && stack.back().first >= level) {
                    stack.pop_back();
                }

                if (stack.empty()) {
                    roots.push_back(node);
                } else {
                    stack.back().second->children.push_back(node);
                }

                stack.push_back({level, node});
            }
        } else {
            // Process standard Markdown links (Mode 2)
            std::sregex_iterator linkIt(lineStr.begin(), lineStr.end(), linkRegex);
            std::sregex_iterator end;

            while (linkIt != end) {
                std::string linkText = (*linkIt)[1].str();
                std::string url = (*linkIt)[2].str();

                // We only care about external links here
                if (!url.empty() && url[0] != '#') {
                    std::string leafLabel = url;

                    // 1. Isolate the leaf by finding the last slash
                    size_t lastSlash = url.find_last_of('/');
                    if (lastSlash != std::string::npos && lastSlash + 1 < url.length()) {
                        leafLabel = url.substr(lastSlash + 1);
                    }

                    // 2. Strip any #fragments or ?parameters from the leaf
                    size_t fragmentPos = leafLabel.find_first_of("#?");
                    if (fragmentPos != std::string::npos) {
                        leafLabel = leafLabel.substr(0, fragmentPos);
                    }

                    // 3. Fallback: if URL was just a root domain with a trailing slash (e.g. "http://sen-labs.org/"),
                    // the leaf is empty. Fall back to the original link text.
                    if (leafLabel.empty()) {
                        leafLabel = linkText;
                    }

                    MarkdownNode* node = new MarkdownNode();
                    node->label = leafLabel;
                    node->offset = static_cast<int32>(currentOffset + linkIt->position());
                    node->line = currentLine;
                    node->isSelf = false;

                    if (stack.empty()) {
                        roots.push_back(node);
                    } else {
                        stack.back().second->children.push_back(node);
                    }
                }
                ++linkIt;
            }

            // Process Wiki links (Always external, so only in Mode 2)
            std::sregex_iterator wikiIt(lineStr.begin(), lineStr.end(), wikiLinkRegex);

            while (wikiIt != end) {
                MarkdownNode* node = new MarkdownNode();
                node->label = (*wikiIt)[1].str();
                node->offset = static_cast<int32>(currentOffset + wikiIt->position());
                node->line = currentLine;
                node->isSelf = false;

                if (stack.empty()) {
                    roots.push_back(node);
                } else {
                    stack.back().second->children.push_back(node);
                }
                ++wikiIt;
            }
        }   // if isSelfRelation

        currentOffset += lineStr.length() + 1;
        currentLine++;
    }   // while

    // Pass the initial mode as the default for any text-links at the absolute root of the document
    SerializeNodes(roots, reply, isSelfRelation);

    for (auto root : roots) {
        delete root;
    }
}

void MarkdownExtractorApp::SerializeNodes(const std::vector<MarkdownNode*>& siblings, BMessage* msg, bool parentIsSelf) {
    BMessage childrenRoot(SENSEI_MESSAGE_RESULT);

    for (const MarkdownNode* node : siblings) {
        childrenRoot.AddString(SENSEI_LABEL, node->label.c_str());
        childrenRoot.AddInt32("offset", node->offset);
        childrenRoot.AddInt32("line", node->line);

        // Crucial: Pass THIS node's isSelf status down.
        // This ensures the recursive call appends the correct target (Self vs <SEN:ID>)
        // to `childrenRoot`'s parallel arrays for this exact index.
        SerializeNodes(node->children, &childrenRoot, node->isSelf);
    }

    msg->AddMessage(SENSEI_ITEM, &childrenRoot);

    if (parentIsSelf) {
        //msg->AddString(SENSEI_ITEM_ID, "");
        msg->AddString(SENSEI_TO, SENSEI_TO_SELF);
    } else {
        //msg->AddString(SENSEI_ITEM_ID, "<SEN:ID>");
        //msg->AddString(SENSEI_TO, "<SEN:ID>");
    }
}

int main() {
    MarkdownExtractorApp* app = new MarkdownExtractorApp();
    if (app->InitCheck() != B_OK) {
        return 1;
    }

    app->Run();

    delete app;
    return 0;
}