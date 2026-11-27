package com.rottr.recomp;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

/** Looks up files in the folder chosen with Android's folder picker. */
final class GameFolder {
    private GameFolder() {}

    /** Returns the named file directly inside the folder (ignoring case), or null. */
    static Uri findFile(ContentResolver resolver, Uri tree, String name) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getTreeDocumentId(tree));
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME};
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            while (cursor != null && cursor.moveToNext()) {
                if (name.equalsIgnoreCase(cursor.getString(1))) {
                    return DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
                }
            }
        }
        return null;
    }
}
