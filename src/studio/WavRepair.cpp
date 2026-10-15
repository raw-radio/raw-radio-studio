// raw-radio-studio — crash-safe WAV header repair (Epic 7 hardening, FR-REC-8).

#include "WavRepair.h"

namespace rrs::wavrepair
{
    namespace
    {
        juce::uint32 readU32LE (const char* p) noexcept
        {
            return (juce::uint32) ((juce::uint8) p[0]
                                   | ((juce::uint32) (juce::uint8) p[1] << 8)
                                   | ((juce::uint32) (juce::uint8) p[2] << 16)
                                   | ((juce::uint32) (juce::uint8) p[3] << 24));
        }

        juce::uint16 readU16LE (const char* p) noexcept
        {
            return (juce::uint16) ((juce::uint8) p[0] | ((juce::uint16) (juce::uint8) p[1] << 8));
        }

        void writeU32LE (char* p, juce::uint32 v) noexcept
        {
            p[0] = (char) (v & 0xff);
            p[1] = (char) ((v >> 8) & 0xff);
            p[2] = (char) ((v >> 16) & 0xff);
            p[3] = (char) ((v >> 24) & 0xff);
        }

        bool isId (const char* p, const char* id) noexcept
        {
            return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3];
        }
    }

    Result repairTruncatedHeader (const juce::File& file)
    {
        Result r;

        if (! file.existsAsFile())
        {
            r.status = Result::Status::notRiff;
            r.detail = "file does not exist";
            return r;
        }

        const auto fileSize = file.getSize();

        if (fileSize < 12)
        {
            r.status = Result::Status::notRiff;
            r.detail = "file too small to be RIFF/WAVE";
            return r;
        }

        juce::FileInputStream in (file);

        if (! in.openedOk())
        {
            r.status = Result::Status::notRiff;
            r.detail = "could not open file";
            return r;
        }

        char riff[12];

        if (in.read (riff, 12) != 12)
        {
            r.status = Result::Status::notRiff;
            r.detail = "short RIFF header";
            return r;
        }

        const bool isRiff = isId (riff, "RIFF");
        const bool isRf64 = isId (riff, "RF64");

        if ((! isRiff && ! isRf64) || ! isId (riff + 8, "WAVE"))
        {
            r.status = Result::Status::notRiff;
            r.detail = "not a RIFF/WAVE file";
            return r;
        }

        if (isRf64)
        {
            // RF64 (>= 4 GB) uses 64-bit size fields; a take never reaches it, and
            // rewriting it here would risk corrupting a valid file.
            r.status = Result::Status::consistent;
            r.detail = "RF64 not handled";
            return r;
        }

        const auto riffDeclared = readU32LE (riff + 4);

        juce::int64 pos = 12;
        juce::int64 blockAlign = 0;

        // Walk the chunk list to locate the `data` chunk. The `fmt ` chunk gives
        // the block alignment used to derive a whole number of frames (and thus to
        // drop a single even-length pad byte JUCE may append).
        while (pos + 8 <= fileSize)
        {
            char ch[8];

            if (! in.setPosition (pos) || in.read (ch, 8) != 8)
            {
                r.status = Result::Status::failed;
                r.detail = "truncated chunk header before data";
                return r;
            }

            const auto chunkSize = (juce::int64) readU32LE (ch + 4);
            const auto bodyPos   = pos + 8;

            if (isId (ch, "fmt "))
            {
                char fmtBody[16] = {};

                if (in.read (fmtBody, (int) juce::jmin ((juce::int64) sizeof (fmtBody), chunkSize)) >= 16)
                    blockAlign = (juce::int64) readU16LE (fmtBody + 12);

                pos = bodyPos + chunkSize + (chunkSize & 1);
                continue;
            }

            if (isId (ch, "data"))
            {
                const auto payload = fileSize - bodyPos;

                r.dataBytes = payload;
                r.sampleFrames = blockAlign > 0 ? payload / blockAlign : 0;

                // The true data length is a whole number of frames; a lone trailing
                // pad byte (JUCE pads odd-length chunks) is not audio.
                juce::int64 trueData = payload;

                if (blockAlign > 0)
                    trueData = (payload / blockAlign) * blockAlign;
                else if (payload & 1)
                    trueData = payload - 1;

                const auto expectedRiff = fileSize - 8;

                if (chunkSize == trueData && riffDeclared == (juce::uint32) expectedRiff)
                {
                    r.status = Result::Status::consistent;
                    r.detail = "header already matches payload (" + juce::String (trueData) + " bytes)";
                    return r;
                }

                if (expectedRiff > (juce::int64) 0xffffffffu || trueData > (juce::int64) 0xffffffffu)
                {
                    r.status = Result::Status::failed;
                    r.detail = "payload exceeds 32-bit RIFF size";
                    return r;
                }

                // JUCE 9 has no RandomAccessFile; a FileOutputStream opens the
                // existing file read/write (no truncation) and is seekable, so we
                // can rewrite the two size fields in place without touching the
                // (potentially huge) sample payload.
                juce::FileOutputStream out (file, 8);

                if (! out.openedOk())
                {
                    r.status = Result::Status::failed;
                    r.detail = "could not open file for writing";
                    return r;
                }

                char dataSizeField[4] {};
                char riffSizeField[4] {};
                writeU32LE (dataSizeField, (juce::uint32) trueData);
                writeU32LE (riffSizeField, (juce::uint32) expectedRiff);

                const auto ok = out.setPosition (bodyPos - 4) && out.write (dataSizeField, 4)
                             && out.setPosition (4) && out.write (riffSizeField, 4);

                if (! ok)
                {
                    r.status = Result::Status::failed;
                    r.detail = "write failed";
                    return r;
                }

                out.flush();

                r.status = Result::Status::repaired;
                r.detail = "repaired header: " + juce::String (trueData) + " data bytes / "
                           + juce::String (r.sampleFrames) + " frames";
                return r;
            }

            pos = bodyPos + chunkSize + (chunkSize & 1);
        }

        r.status = Result::Status::failed;
        r.detail = "no data chunk found";
        return r;
    }
}
