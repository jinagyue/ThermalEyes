package org.opencv.engine;

/**
 * Interface for OpenCV Engine Service generated from OpenCVEngineInterface.aidl.
 */
public interface OpenCVEngineInterface extends android.os.IInterface {
    public static final java.lang.String DESCRIPTOR = "org.opencv.engine.OpenCVEngineInterface";

    public static class Default implements org.opencv.engine.OpenCVEngineInterface {
        @Override public int getEngineVersion() throws android.os.RemoteException { return 0; }
        @Override public java.lang.String getLibPathByVersion(java.lang.String version) throws android.os.RemoteException { return null; }
        @Override public boolean installVersion(java.lang.String version) throws android.os.RemoteException { return false; }
        @Override public java.lang.String getLibraryList(java.lang.String version) throws android.os.RemoteException { return null; }
        @Override public android.os.IBinder asBinder() { return null; }
    }

    public static abstract class Stub extends android.os.Binder implements org.opencv.engine.OpenCVEngineInterface {
        static final int TRANSACTION_getEngineVersion = (android.os.IBinder.FIRST_CALL_TRANSACTION + 0);
        static final int TRANSACTION_getLibPathByVersion = (android.os.IBinder.FIRST_CALL_TRANSACTION + 1);
        static final int TRANSACTION_installVersion = (android.os.IBinder.FIRST_CALL_TRANSACTION + 2);
        static final int TRANSACTION_getLibraryList = (android.os.IBinder.FIRST_CALL_TRANSACTION + 3);

        public Stub() {
            this.attachInterface(this, DESCRIPTOR);
        }

        public static org.opencv.engine.OpenCVEngineInterface asInterface(android.os.IBinder obj) {
            if (obj == null) {
                return null;
            }
            android.os.IInterface iin = obj.queryLocalInterface(DESCRIPTOR);
            if (iin != null && iin instanceof org.opencv.engine.OpenCVEngineInterface) {
                return (org.opencv.engine.OpenCVEngineInterface) iin;
            }
            return new org.opencv.engine.OpenCVEngineInterface.Stub.Proxy(obj);
        }

        @Override public android.os.IBinder asBinder() {
            return this;
        }

        @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws android.os.RemoteException {
            java.lang.String descriptor = DESCRIPTOR;
            if (code >= android.os.IBinder.FIRST_CALL_TRANSACTION && code <= android.os.IBinder.LAST_CALL_TRANSACTION) {
                data.enforceInterface(descriptor);
            }
            if (code == INTERFACE_TRANSACTION) {
                reply.writeString(descriptor);
                return true;
            }
            switch (code) {
                case TRANSACTION_getEngineVersion: {
                    int _result = this.getEngineVersion();
                    reply.writeNoException();
                    reply.writeInt(_result);
                    return true;
                }
                case TRANSACTION_getLibPathByVersion: {
                    java.lang.String _arg0 = data.readString();
                    java.lang.String _result = this.getLibPathByVersion(_arg0);
                    reply.writeNoException();
                    reply.writeString(_result);
                    return true;
                }
                case TRANSACTION_installVersion: {
                    java.lang.String _arg0 = data.readString();
                    boolean _result = this.installVersion(_arg0);
                    reply.writeNoException();
                    reply.writeInt(((_result)?(1):(0)));
                    return true;
                }
                case TRANSACTION_getLibraryList: {
                    java.lang.String _arg0 = data.readString();
                    java.lang.String _result = this.getLibraryList(_arg0);
                    reply.writeNoException();
                    reply.writeString(_result);
                    return true;
                }
                default: {
                    return super.onTransact(code, data, reply, flags);
                }
            }
        }

        private static class Proxy implements org.opencv.engine.OpenCVEngineInterface {
            private android.os.IBinder mRemote;

            Proxy(android.os.IBinder remote) {
                mRemote = remote;
            }

            @Override public android.os.IBinder asBinder() {
                return mRemote;
            }

            public java.lang.String getInterfaceDescriptor() {
                return DESCRIPTOR;
            }

            @Override public int getEngineVersion() throws android.os.RemoteException {
                android.os.Parcel _data = android.os.Parcel.obtain();
                android.os.Parcel _reply = android.os.Parcel.obtain();
                int _result;
                try {
                    _data.writeInterfaceToken(DESCRIPTOR);
                    mRemote.transact(Stub.TRANSACTION_getEngineVersion, _data, _reply, 0);
                    _reply.readException();
                    _result = _reply.readInt();
                } finally {
                    _reply.recycle();
                    _data.recycle();
                }
                return _result;
            }

            @Override public java.lang.String getLibPathByVersion(java.lang.String version) throws android.os.RemoteException {
                android.os.Parcel _data = android.os.Parcel.obtain();
                android.os.Parcel _reply = android.os.Parcel.obtain();
                java.lang.String _result;
                try {
                    _data.writeInterfaceToken(DESCRIPTOR);
                    mRemote.transact(Stub.TRANSACTION_getLibPathByVersion, _data, _reply, 0);
                    _reply.readException();
                    _result = _reply.readString();
                } finally {
                    _reply.recycle();
                    _data.recycle();
                }
                return _result;
            }

            @Override public boolean installVersion(java.lang.String version) throws android.os.RemoteException {
                android.os.Parcel _data = android.os.Parcel.obtain();
                android.os.Parcel _reply = android.os.Parcel.obtain();
                boolean _result;
                try {
                    _data.writeInterfaceToken(DESCRIPTOR);
                    mRemote.transact(Stub.TRANSACTION_installVersion, _data, _reply, 0);
                    _reply.readException();
                    _result = (0 != _reply.readInt());
                } finally {
                    _reply.recycle();
                    _data.recycle();
                }
                return _result;
            }

            @Override public java.lang.String getLibraryList(java.lang.String version) throws android.os.RemoteException {
                android.os.Parcel _data = android.os.Parcel.obtain();
                android.os.Parcel _reply = android.os.Parcel.obtain();
                java.lang.String _result;
                try {
                    _data.writeInterfaceToken(DESCRIPTOR);
                    mRemote.transact(Stub.TRANSACTION_getLibraryList, _data, _reply, 0);
                    _reply.readException();
                    _result = _reply.readString();
                } finally {
                    _reply.recycle();
                    _data.recycle();
                }
                return _result;
            }
        }
    }

    public int getEngineVersion() throws android.os.RemoteException;
    public java.lang.String getLibPathByVersion(java.lang.String version) throws android.os.RemoteException;
    public boolean installVersion(java.lang.String version) throws android.os.RemoteException;
    public java.lang.String getLibraryList(java.lang.String version) throws android.os.RemoteException;
}
