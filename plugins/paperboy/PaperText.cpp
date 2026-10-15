/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperText.h"

#include <cstring>

#include <spdlog/spdlog.h>

extern "C" {
#include <mupdf/fitz.h>
}

namespace {

/** The fields of the document that authors and publishers put a DOI into. */
const char* const kInfoFields[] = {
	"info:Subject", "info:Keywords", "info:doi", "info:DOI", "info:Doi", "info:WPS-ARTICLEDOI", "info:prism:doi",
	"info:Identifier", "info:dc:identifier", "info:Description"
};

/** the text of the biggest lines that have a size worth looking at: names of journals are bigger, but sit in short lines */
constexpr int kMinHeadlineChars = 10;
constexpr int kMaxHeadlineLines = 4;
constexpr float kHeadlineTolerance = 0.92f;
constexpr float kMaxSaneFontSize = 80.0f;

int
LineLength(const fz_stext_line* line)
{
	int count = 0;
	for (const fz_stext_char* ch = line->first_char; ch != NULL; ch = ch->next)
		count++;
	return count;
}

void
AppendLine(fz_context* ctx, fz_buffer* buffer, const fz_stext_line* line)
{
	for (const fz_stext_char* ch = line->first_char; ch != NULL; ch = ch->next)
		fz_append_rune(ctx, buffer, ch->c);
}

/**
 * Reads one page: its text into @p text, and (if @p headline is given) the title-like biggest text of it.
 * Plain C inside fz_try: MuPDF reports errors with longjmp, which would skip destructors.
 * @return 1 if the page was read, 0 if not
 */
int
ReadPage(fz_context* ctx, fz_document* doc, int number, fz_buffer* text, fz_buffer* headline)
{
	fz_page* page = NULL;
	fz_stext_page* structured = NULL;
	fz_output* out = NULL;
	int ok = 1;

	fz_var(page);
	fz_var(structured);
	fz_var(out);

	fz_try(ctx) {
		page = fz_load_page(ctx, doc, number);
		structured = fz_new_stext_page_from_page(ctx, page, NULL);

		out = fz_new_output_with_buffer(ctx, text);
		fz_print_stext_page_as_text(ctx, out, structured);
		fz_close_output(ctx, out);
		fz_append_byte(ctx, text, '\n');

		if (headline != NULL) {
			float biggest = 0;
			for (fz_stext_block* block = structured->first_block; block != NULL; block = block->next) {
				if (block->type != FZ_STEXT_BLOCK_TEXT)
					continue;
				for (fz_stext_line* line = block->u.t.first_line; line != NULL; line = line->next) {
					if (line->first_char != NULL && LineLength(line) >= kMinHeadlineChars
							&& line->first_char->size > biggest && line->first_char->size < kMaxSaneFontSize)
						biggest = line->first_char->size;
				}
			}
			int lines = 0;
			for (fz_stext_block* block = structured->first_block; block != NULL && biggest > 0; block = block->next) {
				if (block->type != FZ_STEXT_BLOCK_TEXT)
					continue;
				for (fz_stext_line* line = block->u.t.first_line; line != NULL && lines < kMaxHeadlineLines; line = line->next) {
					if (line->first_char == NULL || line->first_char->size < biggest * kHeadlineTolerance
							|| line->first_char->size > kMaxSaneFontSize)
						continue;
					if (lines > 0)
						fz_append_byte(ctx, headline, ' ');
					AppendLine(ctx, headline, line);
					lines++;
				}
			}
		}
	}
	fz_always(ctx) {
		fz_drop_output(ctx, out);
		fz_drop_stext_page(ctx, structured);
		fz_drop_page(ctx, page);
	}
	fz_catch(ctx) {
		ok = 0;
	}
	return ok;
}

std::string
CopyBuffer(fz_context* ctx, fz_buffer* buffer)
{
	unsigned char* data = NULL;
	size_t size = fz_buffer_storage(ctx, buffer, &data);
	return std::string(reinterpret_cast<const char*>(data), size);
}

}	// namespace


status_t
ReadDocumentFacts(const std::string& path, int maxPages, DocumentFacts* facts)
{
	fz_context* ctx = fz_new_context(NULL, NULL, 64 * 1024 * 1024);
	if (ctx == NULL)
		return B_NO_MEMORY;

	fz_document* doc = NULL;
	int pageCount = 0;
	int needsPassword = 0;
	int opened = 0;
	char title[1024] = "";
	constexpr size_t kFieldCount = sizeof(kInfoFields) / sizeof(kInfoFields[0]);
	char fields[kFieldCount][2048];
	for (size_t i = 0; i < kFieldCount; i++)
		fields[i][0] = '\0';

	fz_var(doc);
	fz_var(pageCount);
	fz_var(needsPassword);
	fz_var(opened);

	fz_try(ctx) {
		fz_register_document_handlers(ctx);
		doc = fz_open_document(ctx, path.c_str());
		opened = 1;
		if (fz_needs_password(ctx, doc)) {
			needsPassword = 1;
		} else {
			// a book has no pages until it is laid out: use the size of a small page
			if (fz_is_document_reflowable(ctx, doc))
				fz_layout_document(ctx, doc, 480, 720, 11);
			pageCount = fz_count_pages(ctx, doc);
			if (fz_lookup_metadata(ctx, doc, "info:Title", title, sizeof(title)) < 0)
				title[0] = '\0';
			for (size_t i = 0; i < kFieldCount; i++) {
				if (fz_lookup_metadata(ctx, doc, kInfoFields[i], fields[i], sizeof(fields[i])) < 0)
					fields[i][0] = '\0';
			}
		}
	}
	fz_catch(ctx) {
		spdlog::warn("could not open {}: {}", path, fz_caught_message(ctx));
	}

	if (!opened || needsPassword) {
		fz_drop_document(ctx, doc);
		fz_drop_context(ctx);
		return needsPassword ? B_NOT_ALLOWED : B_BAD_DATA;
	}

	facts->pageCount = pageCount;
	facts->infoTitle = title;
	for (size_t i = 0; i < kFieldCount; i++) {
		if (fields[i][0] != '\0')
			facts->metadata.push_back(fields[i]);
	}

	int toRead = pageCount < maxPages ? pageCount : maxPages;
	for (int number = 0; number < toRead; number++) {
		fz_buffer* text = NULL;
		fz_buffer* headline = NULL;
		int ok = 0;
		fz_var(text);
		fz_var(headline);
		fz_var(ok);

		fz_try(ctx) {
			text = fz_new_buffer(ctx, 8192);
			headline = number == 0 ? fz_new_buffer(ctx, 256) : NULL;
			ok = ReadPage(ctx, doc, number, text, headline);
		}
		fz_catch(ctx) {
			ok = 0;
		}

		if (ok) {
			facts->pages.push_back(CopyBuffer(ctx, text));
			if (headline != NULL)
				facts->headline = CopyBuffer(ctx, headline);
		} else {
			facts->pages.push_back("");
			spdlog::debug("page {} of {} could not be read", number + 1, path);
		}
		fz_drop_buffer(ctx, headline);
		fz_drop_buffer(ctx, text);
	}

	fz_drop_document(ctx, doc);
	fz_drop_context(ctx);
	return B_OK;
}
