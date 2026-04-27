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

class MarkdownExtractorApp : public BApplication {
public:
    MarkdownExtractorApp();
    virtual void ArgvReceived(int32 argc, char** argv) override;
    virtual void RefsReceived(BMessage* message) override;

private:
    status_t ProcessMarkdown(const entry_ref* ref, bool isSelfRelation, BMessage* reply);
    void ExtractReferences(std::ifstream& stream, bool isSelfRelation, BMessage* reply);
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

    // DEBUG
    message->PrintToStream();

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
    std::string line;

    // Regex definitions (To be replaced by tree-sitter AST traversal later)
    std::regex headingRegex("^#{1,6}\\s+(.*)");
    std::regex linkRegex("\\[([^\\]]+)\\]\\(([^)]+)\\)");
    std::regex wikiLinkRegex("\\[\\[(.*?)\\]\\]");

    std::streampos currentOffset = 0;

    while (std::getline(stream, line)) {
        std::smatch match;

        // MODE 1: Self Relations (Outlines / Headings)
        if (isSelfRelation && std::regex_search(line, match, headingRegex)) {
            BMessage itemMsg;
            itemMsg.AddString(SENSEI_ITEM_ID, "");
            itemMsg.AddString(SENSEI_TO, SENSEI_TO_SELF);
            itemMsg.AddString(SENSEI_LABEL, match[1].str().c_str());
            itemMsg.AddInt32("offset", static_cast<int32>(currentOffset));

            // Append item as an element in the BMessage array under the key "_item"
            reply->AddMessage(SENSEI_ITEM, &itemMsg);
        }
        // MODE 2: External Relations (Links and Wiki Links)
        else if (!isSelfRelation) {

            // Extract standard Markdown links: [Title](url/file)
            std::sregex_iterator linkIt(line.begin(), line.end(), linkRegex);
            std::sregex_iterator end;
            while (linkIt != end) {
                BMessage itemMsg;
                itemMsg.AddString(SENSEI_ITEM_ID, "<SEN:ID>");
                itemMsg.AddString(SENSEI_TO, "<SEN:TO>");
                itemMsg.AddString(SENSEI_LABEL, (*linkIt)[1].str().c_str());
                itemMsg.AddInt32("offset", static_cast<int32>(currentOffset + linkIt->position()));

                reply->AddMessage(SENSEI_ITEM, &itemMsg);
                ++linkIt;
            }

            // Extract Wiki links: [[Wiki Link]]
            std::sregex_iterator wikiIt(line.begin(), line.end(), wikiLinkRegex);
            while (wikiIt != end) {
                BMessage itemMsg;
                itemMsg.AddString(SENSEI_ITEM_ID, "<SEN:ID>");
                itemMsg.AddString(SENSEI_TO, "<SEN:TO>");
                itemMsg.AddString(SENSEI_LABEL, (*wikiIt)[1].str().c_str());
                itemMsg.AddInt32("offset", static_cast<int32>(currentOffset + wikiIt->position()));

                reply->AddMessage(SENSEI_ITEM, &itemMsg);
                ++wikiIt;
            }
        }

        // Update the textual offset (+1 accounts for the newline character consumed by std::getline)
        currentOffset += line.length() + 1;
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